/* Short, event-driven thumb feedback. No timer runs between transitions. GPLv3+. */
#pragma once
class NativeThumbAnimation {
    float m_value = 6.0f, m_from = 6.0f, m_target = 6.0f;
    ULONGLONG m_start = 0;
public:
    static constexpr UINT_PTR Timer = 0x7f31;
    float Value() const { return m_value; }
    void State(CWnd& owner, bool hover, bool dragging) {
        const float target = dragging ? 4.0f : hover ? 10.0f : 6.0f;
        if (target == m_target) return;
        m_from = m_value; m_target = target; m_start = GetTickCount64();
        owner.SetTimer(Timer, 16, nullptr);
        owner.Invalidate(FALSE);
    }
    bool Tick(CWnd& owner, UINT_PTR id) {
        if (id != Timer) return false;
        const float t = std::min(1.0f, float(GetTickCount64() - m_start) / 130.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        m_value = m_from + (m_target - m_from) * eased;
        if (t >= 1.0f || !owner.IsWindowVisible()) {
            m_value = m_target; owner.KillTimer(Timer);
        }
        owner.Invalidate(FALSE);
        return true;
    }
};
