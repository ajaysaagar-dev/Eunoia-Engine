#pragma once
// ============================================================================
// Eunoia Engine — Custom UI Manager
// Central coordinator managing UI trees, dispatching input, and issuing
// DirectX 12 render passes without ImGui.
// ============================================================================

#include "CustomUITypes.h"
#include "CustomUIWidget.h"
#include "CustomUIRenderer.h"
#include <vector>
#include <memory>
#include <d3d12.h>

namespace Eunoia { namespace UI {

class CustomUIManager {
public:
    static CustomUIManager& Get() {
        static CustomUIManager instance;
        return instance;
    }

    bool Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Canvas Management
    std::shared_ptr<CustomUICanvas> CreateCanvas(const std::string& name = "Canvas");
    std::shared_ptr<CustomUICanvas> GetRootCanvas() { return m_rootCanvas; }

    // Top Menu Bar shortcut
    std::shared_ptr<CustomUITopMenuBar> CreateTopMenuBar();
    std::shared_ptr<CustomUITopMenuBar> GetTopMenuBar() { return m_topMenuBar; }

    // Frame update & input dispatching
    void Update(float deltaTime);
    bool ProcessInput(float mouseX, float mouseY, bool leftDown, bool leftClicked, bool leftReleased, float scrollDelta = 0.0f);

    // Frame rendering into DirectX 12 command list
    void Render(ID3D12GraphicsCommandList* cmdList, uint32_t screenWidth, uint32_t screenHeight);

    // Active visibility
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

private:
    CustomUIManager() = default;
    ~CustomUIManager() = default;

    bool m_initialized = false;
    bool m_enabled = true;
    std::shared_ptr<CustomUICanvas> m_rootCanvas;
    std::shared_ptr<CustomUITopMenuBar> m_topMenuBar;
    std::vector<std::shared_ptr<CustomUICanvas>> m_canvases;

    glm::vec2 m_lastMousePos{ 0.0f, 0.0f };
    bool m_wasMouseDown = false;
};

// Global shorthand APIs for developers and gameplay scripts:
namespace CustomUI {
    inline bool Initialize(ID3D12Device* device) {
        return CustomUIManager::Get().Initialize(device);
    }
    inline void Shutdown() {
        CustomUIManager::Get().Shutdown();
    }
    inline std::shared_ptr<CustomUICanvas> GetRootCanvas() {
        return CustomUIManager::Get().GetRootCanvas();
    }
    inline std::shared_ptr<CustomUIPanel> CreatePanel(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) {
        auto panel = std::make_shared<CustomUIPanel>();
        panel->SetPosition(pos);
        panel->SetSize(size);
        panel->GetStyle().backgroundColor = color;
        CustomUIManager::Get().GetRootCanvas()->AddChild(panel);
        return panel;
    }
    inline std::shared_ptr<CustomUIButton> CreateButton(const std::string& text, const glm::vec2& pos, const glm::vec2& size, UIClickCallback onClick = nullptr) {
        auto btn = std::make_shared<CustomUIButton>(text);
        btn->SetPosition(pos);
        btn->SetSize(size);
        btn->SetOnClick(onClick);
        CustomUIManager::Get().GetRootCanvas()->AddChild(btn);
        return btn;
    }
    inline std::shared_ptr<CustomUILabel> CreateLabel(const std::string& text, const glm::vec2& pos, float fontSize = 16.0f, const glm::vec4& color = glm::vec4(1.0f)) {
        auto lbl = std::make_shared<CustomUILabel>(text);
        lbl->SetPosition(pos);
        lbl->SetFontSize(fontSize);
        lbl->SetTextColor(color);
        CustomUIManager::Get().GetRootCanvas()->AddChild(lbl);
        return lbl;
    }
}

}} // namespace Eunoia::UI
