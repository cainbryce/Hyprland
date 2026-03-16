#include "ScreenshareManager.hpp"
#include "../../render/Renderer.hpp"
#include "../../Compositor.hpp"
#include "../../desktop/view/Window.hpp"
#include "../../protocols/core/Seat.hpp"

using namespace Screenshare;

CScreenshareManager::CScreenshareManager() {
    ;
}

void CScreenshareManager::onOutputCommit(PHLMONITOR monitor) {
    std::erase_if(m_sessions, [&](const WP<CScreenshareSession>& session) { return session.expired(); });

    // if no pending frames, and no sessions are sharing, then unblock ds
    if (m_pendingFrames.empty()) {
        if (std::ranges::none_of(m_sessions, [](const auto& s) { return !s.expired() && !s->m_stopped && s->m_sharing; }))
            g_pHyprRenderer->m_directScanoutBlocked = false;
        return;
    }

    std::ranges::for_each(m_pendingFrames, [&](WP<CScreenshareFrame>& frame) {
        if (frame.expired() || !frame->m_shared || frame->done())
            return;

        if (frame->m_session->monitor() != monitor)
            return;

        if (frame->m_session->m_type == SHARE_WINDOW) {
            CBox geometry = {frame->m_session->m_window->m_realPosition->value(), frame->m_session->m_window->m_realSize->value()};
            if (geometry.intersection({monitor->m_position, monitor->m_size}).empty())
                return;
        }

        frame->copy();
    });

    std::erase_if(m_pendingFrames, [&](const WP<CScreenshareFrame>& frame) { return frame.expired(); });
}

void CScreenshareManager::onMonitorDamage(PHLMONITOR monitor, const CRegion& damage) {
    for (const auto& session : m_sessions) {
        if (session.expired() || session->m_stopped)
            continue;

        if (session->monitor() != monitor)
            continue;

        if (session->m_type == SHARE_MONITOR)
            session->accumulateDamage(damage);
        else if (session->m_type == SHARE_REGION) {
            // translate damage to the capture box coordinate space
            CRegion translated = damage.copy().translate(-session->m_captureBox.pos());
            session->accumulateDamage(translated);
        } else if (session->m_type == SHARE_WINDOW) {
            // window damage is accumulated when the window surface commits, but
            // monitor damage may still be relevant (e.g. overlapping windows)
            session->accumulateDamage(damage);
        }
    }
}

UP<CScreenshareSession> CScreenshareManager::newSession(wl_client* client, PHLMONITOR monitor) {
    if UNLIKELY (!monitor || !g_pCompositor->monitorExists(monitor)) {
        LOGM(Log::ERR, "Client requested sharing of a monitor that is gone");
        return nullptr;
    }

    UP<CScreenshareSession> session = UP<CScreenshareSession>(new CScreenshareSession(monitor, client));

    session->m_self = session;
    m_sessions.emplace_back(session);

    return session;
}

UP<CScreenshareSession> CScreenshareManager::newSession(wl_client* client, PHLMONITOR monitor, CBox captureRegion) {
    if UNLIKELY (!monitor || !g_pCompositor->monitorExists(monitor)) {
        LOGM(Log::ERR, "Client requested sharing of a monitor that is gone");
        return nullptr;
    }

    UP<CScreenshareSession> session = UP<CScreenshareSession>(new CScreenshareSession(monitor, captureRegion, client));

    session->m_self = session;
    m_sessions.emplace_back(session);

    return session;
}

UP<CScreenshareSession> CScreenshareManager::newSession(wl_client* client, PHLWINDOW window) {
    if UNLIKELY (!window || !window->m_isMapped) {
        LOGM(Log::ERR, "Client requested sharing of window that is gone or not shareable!");
        return nullptr;
    }

    UP<CScreenshareSession> session = UP<CScreenshareSession>(new CScreenshareSession(window, client));

    session->m_self = session;
    m_sessions.emplace_back(session);

    return session;
}

UP<CCursorshareSession> CScreenshareManager::newCursorSession(wl_client* client, WP<CWLPointerResource> pointer) {
    UP<CCursorshareSession> session = UP<CCursorshareSession>(new CCursorshareSession(client, pointer));

    session->m_self = session;
    m_cursorSessions.emplace_back(session);

    return session;
}

WP<CScreenshareSession> CScreenshareManager::getManagedSession(wl_client* client, PHLMONITOR monitor) {
    return getManagedSession(SHARE_MONITOR, client, monitor, nullptr, {});
}

WP<CScreenshareSession> CScreenshareManager::getManagedSession(wl_client* client, PHLMONITOR monitor, CBox captureBox) {

    return getManagedSession(SHARE_REGION, client, monitor, nullptr, captureBox);
}

WP<CScreenshareSession> CScreenshareManager::getManagedSession(wl_client* client, PHLWINDOW window) {
    return getManagedSession(SHARE_WINDOW, client, nullptr, window, {});
}

WP<CScreenshareSession> CScreenshareManager::getManagedSession(eScreenshareType type, wl_client* client, PHLMONITOR monitor, PHLWINDOW window, CBox captureBox) {
    if (type == SHARE_NONE)
        return {};

    auto it = std::ranges::find_if(m_managedSessions, [&](const auto& session) {
        if (session->m_session->m_client != client || session->m_session->m_type != type)
            return false;

        switch (type) {
            case SHARE_MONITOR: return session->m_session->m_monitor == monitor;
            case SHARE_WINDOW: return session->m_session->m_window == window;
            case SHARE_REGION: return session->m_session->m_monitor == monitor && session->m_session->m_captureBox == captureBox;
            case SHARE_NONE:
            default: return false;
        }

        return false;
    });

    if (it == m_managedSessions.end()) {
        UP<CScreenshareSession> session;
        switch (type) {
            case SHARE_MONITOR: session = UP<CScreenshareSession>(new CScreenshareSession(monitor, client)); break;
            case SHARE_WINDOW: session = UP<CScreenshareSession>(new CScreenshareSession(window, client)); break;
            case SHARE_REGION: session = UP<CScreenshareSession>(new CScreenshareSession(monitor, captureBox, client)); break;
            case SHARE_NONE:
            default: return {};
        }

        session->m_self = session;
        m_sessions.emplace_back(session);

        it = m_managedSessions.emplace(m_managedSessions.end(), makeUnique<SManagedSession>(std::move(session)));
    }

    auto& session = *it;

    session->stoppedListener = session->m_session->m_events.stopped.listen([session = WP<SManagedSession>(session)]() {
        if (!session.expired())
            std::erase_if(Screenshare::mgr()->m_managedSessions, [&](const auto& s) { return s && s->m_session.get() == session->m_session.get(); });
    });

    return session->m_session;
}

bool CScreenshareManager::isOutputBeingSSd(PHLMONITOR monitor) {
    return std::ranges::any_of(m_sessions, [monitor](const auto& s) {
        if (!s)
            return false;
        return (s->m_type == SHARE_MONITOR || s->m_type == SHARE_REGION) && s->m_monitor == monitor;
    });
}

CScreenshareManager::SManagedSession::SManagedSession(UP<CScreenshareSession>&& session) : m_session(std::move(session)) {
    ;
}
