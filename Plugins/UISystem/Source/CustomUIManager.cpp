// ============================================================================
// Eunoia Engine — Custom UI Manager Implementation
// ============================================================================

#include "UISystem/CustomUI/CustomUIManager.h"

namespace Eunoia { namespace UI {

bool CustomUIManager::Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    if (!device) return false;

    if (!CustomUIRenderer::Get().Initialize(device, rtvFormat)) {
        return false;
    }

    m_rootCanvas = std::make_shared<CustomUICanvas>("RootCanvas");
    m_canvases.clear();
    m_canvases.push_back(m_rootCanvas);

    m_initialized = true;
    return true;
}

void CustomUIManager::Shutdown() {
    CustomUIRenderer::Get().Shutdown();
    m_topMenuBar.reset();
    m_canvases.clear();
    m_rootCanvas.reset();
    m_initialized = false;
}

std::shared_ptr<CustomUICanvas> CustomUIManager::CreateCanvas(const std::string& name) {
    auto canvas = std::make_shared<CustomUICanvas>(name);
    m_canvases.push_back(canvas);
    return canvas;
}

std::shared_ptr<CustomUITopMenuBar> CustomUIManager::CreateTopMenuBar() {
    if (!m_topMenuBar) {
        m_topMenuBar = std::make_shared<CustomUITopMenuBar>("EngineTopMenuBar");
    }
    return m_topMenuBar;
}

void CustomUIManager::Update(float /*deltaTime*/) {
    // Reserved for UI animations, transitions, and tweening
}

bool CustomUIManager::ProcessInput(float mouseX, float mouseY, bool leftDown, bool leftClicked, bool leftReleased, float scrollDelta) {
    if (!m_initialized || !m_enabled) return false;

    glm::vec2 mousePos(mouseX, mouseY);
    bool handled = false;

    // 1. Mouse Move Event
    if (mousePos != m_lastMousePos) {
        UIEvent ev;
        ev.type = UIEvent::Type::MouseMove;
        ev.mousePos = mousePos;

        if (m_topMenuBar && m_topMenuBar->HandleEvent(ev)) handled = true;
        if (!handled && m_rootCanvas && m_rootCanvas->HandleEvent(ev)) handled = true;
        m_lastMousePos = mousePos;
    }

    // 2. Mouse Click / Down Event
    if (leftClicked || (leftDown && !m_wasMouseDown)) {
        UIEvent ev;
        ev.type = UIEvent::Type::MouseDown;
        ev.mousePos = mousePos;
        ev.mouseButton = 0;

        if (m_topMenuBar && m_topMenuBar->HandleEvent(ev)) handled = true;
        if (!handled && m_rootCanvas && m_rootCanvas->HandleEvent(ev)) handled = true;
    }

    // 3. Mouse Up Event
    if (leftReleased || (!leftDown && m_wasMouseDown)) {
        UIEvent ev;
        ev.type = UIEvent::Type::MouseUp;
        ev.mousePos = mousePos;
        ev.mouseButton = 0;

        if (m_topMenuBar && m_topMenuBar->HandleEvent(ev)) handled = true;
        if (!handled && m_rootCanvas && m_rootCanvas->HandleEvent(ev)) handled = true;
    }

    // 4. Scroll Event
    if (scrollDelta != 0.0f) {
        UIEvent ev;
        ev.type = UIEvent::Type::Scroll;
        ev.mousePos = mousePos;
        ev.scrollDelta = scrollDelta;

        if (m_rootCanvas && m_rootCanvas->HandleEvent(ev)) handled = true;
    }

    m_wasMouseDown = leftDown;
    return handled;
}

void CustomUIManager::Render(ID3D12GraphicsCommandList* cmdList, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized || !m_enabled || !cmdList) return;

    auto& renderer = CustomUIRenderer::Get();
    renderer.BeginFrame(screenWidth, screenHeight);

    // 1. Render all active UI canvases
    for (auto& canvas : m_canvases) {
        if (canvas && canvas->IsVisible()) {
            canvas->UpdateCanvas(screenWidth, screenHeight);
            canvas->Render();
        }
    }

    // 2. Render Top Menu Bar on top of canvases
    if (m_topMenuBar && m_topMenuBar->IsVisible()) {
        m_topMenuBar->UpdateLayout(glm::vec4(0.0f, 0.0f, (float)screenWidth, 28.0f));
        m_topMenuBar->Render();
    }

    renderer.EndFrame();
    renderer.Render(cmdList);
}

}} // namespace Eunoia::UI
