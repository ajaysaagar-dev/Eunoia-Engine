#include <Node-Behaviour/NodeBehaviourEditor.h>
#include <Node-Behaviour/NodeBehaviourManager.h>
#include <EngineScene/ScreenPrint.h>
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <imgui_node_editor.h>
#include <drawing.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ed = ax::NodeEditor;

namespace Eunoia {
namespace NodeBehaviour {

NodeBehaviourEditor::NodeBehaviourEditor(std::shared_ptr<NodeBehaviourGraph> graph)
    : m_graph(graph)
{
    ed::Config config;
    config.SettingsFile = nullptr; // Don't write settings to arbitrary current working directory
    m_context = ed::CreateEditor(&config);

    if (m_graph && m_graph->nodes.empty()) {
        m_graph->InitializeDefaultEvents();
        m_isDirty = true;
    }
}

NodeBehaviourEditor::~NodeBehaviourEditor() {
    if (m_context) {
        ed::DestroyEditor(m_context);
        m_context = nullptr;
    }
}

void NodeBehaviourEditor::Save() {
    if (m_graph && !m_graph->filePath.empty()) {
        // Save current canvas node positions to graph before serializing
        if (m_context) {
            ed::SetCurrentEditor(m_context);
            for (auto& node : m_graph->nodes) {
                ImVec2 pos = ed::GetNodePosition(node.id);
                if (std::isfinite(pos.x) && std::isfinite(pos.y)) {
                    node.position = pos;
                }
            }
            ed::SetCurrentEditor(nullptr);
        }

        if (m_graph->SaveToFile(m_graph->filePath)) {
            m_isDirty = false;
            // Compile & Synchronize to Scene instances immediately
            NodeBehaviourManager::Get().NotifyGraphSaved(m_graph->filePath, m_graph, m_scene);
        }
    }
}

void NodeBehaviourEditor::Compile() {
    Save();
    if (m_graph) {
        ScreenPrint::System::Get().AddMessage("Compiled " + m_graph->name + " successfully", 3.0f, glm::vec4(0.2f, 0.95f, 0.4f, 1.0f));
        std::cout << "[Node-Behaviour] Compiled and synchronized '" << m_graph->name << "' to scene.\n";
    }
}

void NodeBehaviourEditor::Render(const ImVec2& position, const ImVec2& size) {
    if (!m_graph) return;

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.071f, 0.071f, 0.078f, 1.0f)); // #121214

    std::string winId = "###NodeBehaviourEditor_" + std::to_string((uintptr_t)this);
    if (ImGui::Begin(winId.c_str(), nullptr, flags)) {
        // Ctrl+S hotkey to save and compile
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            Compile();
        }

        float toolbarHeight = 36.0f;
        float sidebarWidth = 280.0f;
        float canvasWidth = std::max(size.x - sidebarWidth, 200.0f);
        float canvasHeight = std::max(size.y - toolbarHeight, 200.0f);

        // 1. Top Toolbar
        RenderTopToolbar();

        // 2. Center: Canvas (handles nodes, links, background context menu & search popup)
        ImGui::SetCursorPos(ImVec2(0.0f, toolbarHeight));
        RenderCanvas(ImVec2(canvasWidth, canvasHeight));

        // 3. Right Sidebar
        ImGui::SetCursorPos(ImVec2(canvasWidth, toolbarHeight));
        RenderRightSidebar(ImVec2(sidebarWidth, canvasHeight));
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void NodeBehaviourEditor::RenderTopToolbar() {
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.09f, 0.10f, 1.0f));
    if (ImGui::BeginChild("##NBToolbar", ImVec2(ImGui::GetWindowWidth(), 36.0f), false, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetCursorPosY(6.0f);
        ImGui::SetCursorPosX(12.0f);

        // Compile Button (Unreal Engine Blueprint style)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.65f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("⚡ Compile", ImVec2(80.0f, 24.0f))) {
            Compile();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compile graph and synchronize exposed variables and logic to scene (Ctrl+S)");
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 6.0f);

        // Save Button
        ImGui::PushStyleColor(ImGuiCol_Button, m_isDirty ? ImVec4(0.063f, 0.725f, 0.506f, 1.0f) : ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button(m_isDirty ? " Save * " : " Saved ", ImVec2(75.0f, 24.0f))) {
            Compile();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save and compile node graph to disk");
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0.0f, 6.0f);

        // Add Node Button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.38f, 0.65f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("+ Add Node", ImVec2(90.0f, 24.0f))) {
            m_openSearchPopup = true;
            m_searchPopupPos = ImVec2(ImGui::GetWindowPos().x + 20.0f, ImGui::GetWindowPos().y + 40.0f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open node creation menu (or Right-Click on canvas background)");
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 12.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Node Behaviour:");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", m_graph->name.c_str());

        if (!m_graph->filePath.empty()) {
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::TextDisabled("(%s)", m_graph->filePath.c_str());
        }

        // Right side info
        float rightTextW = 200.0f;
        if (ImGui::GetContentRegionAvail().x > rightTextW) {
            ImGui::SameLine(ImGui::GetWindowWidth() - rightTextW);
            ImGui::TextDisabled("Right-Click on canvas to add nodes");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void NodeBehaviourEditor::RenderCanvas(const ImVec2& size) {
    if (!m_context) return;

    ed::SetCurrentEditor(m_context);
    ed::Begin("###NodeEditorCanvas", size);

    // Initial positioning for nodes
    for (auto& node : m_graph->nodes) {
        if (!node.hasInitializedPos) {
            ed::SetNodePosition(node.id, node.position);
            node.hasInitializedPos = true;
        }
    }

    // Render Nodes
    for (auto& node : m_graph->nodes) {
        RenderNode(node);
    }

    // Render Links
    for (const auto& link : m_graph->links) {
        ed::Link(link.id, link.startPinId, link.endPinId, ImColor(link.color), 2.5f);
    }

    // Link Creation Interaction
    if (ed::BeginCreate()) {
        ed::PinId inputPinId, outputPinId;
        if (ed::QueryNewLink(&inputPinId, &outputPinId)) {
            uint32_t pinA = (uint32_t)inputPinId.Get();
            uint32_t pinB = (uint32_t)outputPinId.Get();

            if (m_graph->CanCreateLink(pinA, pinB)) {
                if (ed::AcceptNewItem()) {
                    m_graph->AddLink(pinA, pinB);
                    m_isDirty = true;
                }
            } else {
                ed::RejectNewItem(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), 2.0f);
            }
        }
    }
    ed::EndCreate();

    // Deletion Interaction
    if (ed::BeginDelete()) {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            if (ed::AcceptDeletedItem()) {
                m_graph->RemoveLink((uint32_t)deletedLinkId.Get());
                m_isDirty = true;
            }
        }

        ed::NodeId deletedNodeId;
        while (ed::QueryDeletedNode(&deletedNodeId)) {
            if (ed::AcceptDeletedItem()) {
                m_graph->RemoveNode((uint32_t)deletedNodeId.Get());
                m_isDirty = true;
            }
        }
    }
    ed::EndDelete();

    // Right-Click Context Menu Trigger on Canvas
    bool openContextMenu = false;
    if (ed::ShowBackgroundContextMenu()) {
        openContextMenu = true;
    }

    // Fallback right-click release directly on the canvas background
    if (!openContextMenu &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f))
    {
        if (!ed::GetHoveredNode().Get() && !ed::GetHoveredPin().Get() && !ed::GetHoveredLink().Get()) {
            openContextMenu = true;
        }
    }

    if (openContextMenu || m_openSearchPopup) {
        m_searchPopupPos = ImGui::GetMousePos();
        m_searchFilterBuf[0] = '\0';
        m_focusSearchInput = true;
        m_openSearchPopup = false;
        ed::Suspend();
        ImGui::OpenPopup("NodeActionSearchPopup");
        ed::Resume();
    }

    ed::Suspend();
    RenderSearchPopup();

    // Variable Drag & Drop onto Canvas
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NODE_VAR_NAME")) {
            std::string varName = (const char*)payload->Data;
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 canvasPos = ed::ScreenToCanvas(mousePos);
            m_graph->CreateVariableGetNode(varName, canvasPos);
            m_isDirty = true;
        }
        ImGui::EndDragDropTarget();
    }
    ed::Resume();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void NodeBehaviourEditor::RenderNode(NodeItem& node) {
    // 1. Calculate proportional layout widths
    float titleW = ImGui::CalcTextSize(node.name.c_str()).x;
    float subW = node.subtitle.empty() ? 0.0f : ImGui::CalcTextSize(node.subtitle.c_str()).x;
    float headerNeededW = std::max(titleW, subW) + 28.0f;

    float maxInputW = 0.0f;
    for (const auto& pin : node.inputs) {
        float w = 14.0f + 6.0f + ImGui::CalcTextSize(pin.name.c_str()).x;
        if (pin.type != NodePinType::Exec) {
            w += 90.0f; // Space for inline editor if disconnected
        }
        if (w > maxInputW) maxInputW = w;
    }

    float maxOutputW = 0.0f;
    for (const auto& pin : node.outputs) {
        float w = ImGui::CalcTextSize(pin.name.c_str()).x + 6.0f + 14.0f;
        if (w > maxOutputW) maxOutputW = w;
    }

    float minNodeWidth = (node.category == NodeCategory::Event) ? 210.0f : 180.0f;
    if (node.category == NodeCategory::VariableGet) minNodeWidth = 140.0f;

    float targetWidth = std::max({ minNodeWidth, headerNeededW, maxInputW + maxOutputW + 36.0f });

    // Node Styling (Unreal Engine Blueprint aesthetics)
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8.0f, 6.0f, 8.0f, 8.0f));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);

    ed::BeginNode(node.id);

    // Node Header
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 headerStart = ImGui::GetCursorScreenPos();
    
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::TextUnformatted(node.name.c_str());
    ImGui::PopStyleColor();

    if (!node.subtitle.empty()) {
        ImGui::TextColored(ImVec4(0.88f, 0.88f, 0.88f, 0.75f), "%s", node.subtitle.c_str());
    }

    // Force node to targeted width
    ImGui::Dummy(ImVec2(targetWidth - 16.0f, 0.0f));

    float headerEndPosY = ImGui::GetCursorScreenPos().y;
    float headerContentH = headerEndPosY - headerStart.y;

    // Spacing between header and body (NO ImGui::Separator!)
    ImGui::Dummy(ImVec2(targetWidth - 16.0f, 6.0f));

    // Body: Inputs on left, Outputs on right
    ImGui::BeginGroup(); // Inputs
    for (auto& pin : node.inputs) {
        ed::BeginPin(pin.id, ed::PinKind::Input);

        // Determine if pin is connected
        bool isConnected = false;
        for (const auto& l : m_graph->links) {
            if (l.endPinId == pin.id) { isConnected = true; break; }
        }

        ImVec2 iconPos = ImGui::GetCursorScreenPos();
        float iconSize = 14.0f;
        ImVec2 iconMin = ImVec2(iconPos.x, iconPos.y + 2.0f);
        ImVec2 iconMax = ImVec2(iconPos.x + iconSize, iconPos.y + 2.0f + iconSize);

        ax::Drawing::IconType iconType = (pin.type == NodePinType::Exec) ? ax::Drawing::IconType::Flow : ax::Drawing::IconType::Circle;
        ax::Drawing::DrawIcon(drawList, iconMin, iconMax, iconType, isConnected, GetPinColor(pin.type), 0);

        ImGui::Dummy(ImVec2(iconSize + 4.0f, iconSize));
        ImGui::SameLine();
        ImGui::TextUnformatted(pin.name.c_str());

        // Inline default value editor for data pins when disconnected
        if (!isConnected && pin.type != NodePinType::Exec) {
            ImGui::SameLine();
            ImGui::PushItemWidth(80.0f);
            std::string inputId = "##PinVal_" + std::to_string(pin.id);
            if (pin.type == NodePinType::Float) {
                if (ImGui::DragFloat(inputId.c_str(), &pin.floatValue, 0.1f, 0.0f, 0.0f, "%.2f")) {
                    m_isDirty = true;
                }
            } else if (pin.type == NodePinType::Int) {
                if (ImGui::DragInt(inputId.c_str(), &pin.intValue, 1.0f)) {
                    m_isDirty = true;
                }
            } else if (pin.type == NodePinType::Bool) {
                if (ImGui::Checkbox(inputId.c_str(), &pin.boolValue)) {
                    m_isDirty = true;
                }
            } else if (pin.type == NodePinType::String) {
                char strBuf[128];
                strncpy(strBuf, pin.stringValue.c_str(), sizeof(strBuf) - 1);
                strBuf[sizeof(strBuf) - 1] = '\0';
                if (ImGui::InputText(inputId.c_str(), strBuf, sizeof(strBuf))) {
                    pin.stringValue = strBuf;
                    m_isDirty = true;
                }
            } else if (pin.type == NodePinType::Vector3) {
                if (ImGui::DragFloat3(inputId.c_str(), &pin.vectorValue.x, 0.1f)) {
                    m_isDirty = true;
                }
            } else if (pin.type == NodePinType::GameObject) {
                ImGui::TextDisabled("(Self)");
            }
            ImGui::PopItemWidth();
        }

        ed::EndPin();
    }
    ImGui::EndGroup(); // Inputs
    float inputsActualW = ImGui::GetItemRectSize().x;

    // Outputs Group (Flush to Right)
    if (!node.outputs.empty()) {
        float spaceBetween = targetWidth - 16.0f - inputsActualW - maxOutputW;
        if (spaceBetween < 16.0f) spaceBetween = 16.0f;
        ImGui::SameLine(0.0f, spaceBetween);

        ImGui::BeginGroup(); // Outputs
        for (auto& pin : node.outputs) {
            ed::BeginPin(pin.id, ed::PinKind::Output);

            bool isConnected = false;
            for (const auto& l : m_graph->links) {
                if (l.startPinId == pin.id) { isConnected = true; break; }
            }

            ImGui::TextUnformatted(pin.name.c_str());
            ImGui::SameLine();

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            float iconSize = 14.0f;
            ImVec2 iconMin = ImVec2(iconPos.x, iconPos.y + 2.0f);
            ImVec2 iconMax = ImVec2(iconPos.x + iconSize, iconPos.y + 2.0f + iconSize);

            ax::Drawing::IconType iconType = (pin.type == NodePinType::Exec) ? ax::Drawing::IconType::Flow : ax::Drawing::IconType::Circle;
            ax::Drawing::DrawIcon(drawList, iconMin, iconMax, iconType, isConnected, GetPinColor(pin.type), 0);
            ImGui::Dummy(ImVec2(iconSize, iconSize));

            ed::EndPin();
        }
        ImGui::EndGroup(); // Outputs
    }

    ed::EndNode();
    ed::PopStyleVar(3);

    // Draw header background banner on the node background draw list
    ImDrawList* bgDrawList = ed::GetNodeBackgroundDrawList(node.id);
    if (bgDrawList) {
        ImVec2 nodePos = ed::GetNodePosition(node.id);
        ImVec2 nodeSize = ed::GetNodeSize(node.id);
        if (nodeSize.x > 0 && nodeSize.y > 0) {
            float bannerH = headerContentH + 12.0f;
            ImU32 headCol = ImColor(node.headerColor);
            bgDrawList->AddRectFilled(
                nodePos,
                ImVec2(nodePos.x + nodeSize.x, nodePos.y + bannerH),
                headCol,
                5.0f,
                ImDrawFlags_RoundCornersTop
            );
            // Subtle crisp divider line separating header banner from node body
            bgDrawList->AddLine(
                ImVec2(nodePos.x, nodePos.y + bannerH),
                ImVec2(nodePos.x + nodeSize.x, nodePos.y + bannerH),
                IM_COL32(20, 20, 24, 230),
                1.0f
            );
        }
    }
}

void NodeBehaviourEditor::RenderRightSidebar(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.07f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    if (ImGui::BeginChild("##NBSidebar", size, false)) {
        float halfH = (size.y - 20.0f) * 0.5f;

        // TOP PART: Variables Creation & List
        RenderVariablesList(halfH);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // SECOND PART: Selected Variable Details
        RenderVariableDetails(halfH);
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void NodeBehaviourEditor::RenderVariablesList(float height) {
    ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.0f, 1.0f), "VARIABLES");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);

    // "+ Variable" Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.000f, 0.400f, 1.000f, 1.0f)); // #0066ff
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("+ Var", ImVec2(60.0f, 20.0f))) {
        NodeVariable* newV = m_graph->AddVariable("NewVar", NodePinType::Float);
        if (newV) {
            m_selectedVarId = newV->id;
            m_isDirty = true;
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();

    if (ImGui::BeginChild("##VarsScrollList", ImVec2(0.0f, height - 40.0f), true)) {
        if (m_graph->variables.empty()) {
            ImGui::TextDisabled("No variables created.\nClick '+ Var' to add one.");
        }

        uint32_t varToDelete = 0;
        for (const auto& var : m_graph->variables) {
            ImGui::PushID(var.id);

            bool isSelected = (m_selectedVarId == var.id);

            // Type color badge
            ImVec4 typeColor = ImColor(GetPinColor(var.type));
            ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
            ImGui::TextUnformatted("[*]");
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Selectable variable name
            std::string label = var.name + " (" + NodePinTypeToString(var.type) + ")";
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedVarId = var.id;
            }

            // Drag to spawn Get/Set variable
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("NODE_VAR_NAME", var.name.c_str(), var.name.size() + 1);
                ImGui::Text("Variable: %s", var.name.c_str());
                ImGui::EndDragDropSource();
            }

            // Right-click context to add Get/Set
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(("Add 'Get " + var.name + "' to Canvas").c_str())) {
                    m_graph->CreateVariableGetNode(var.name, ImVec2(200.0f, 200.0f));
                    m_isDirty = true;
                }
                if (var.type != NodePinType::GameObject) {
                    if (ImGui::MenuItem(("Add 'Set " + var.name + "' to Canvas").c_str())) {
                        m_graph->CreateVariableSetNode(var.name, ImVec2(200.0f, 200.0f));
                        m_isDirty = true;
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Variable")) {
                    varToDelete = var.id;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        if (varToDelete != 0) {
            m_graph->RemoveVariable(varToDelete);
            if (m_selectedVarId == varToDelete) m_selectedVarId = 0;
            m_isDirty = true;
        }
    }
    ImGui::EndChild();
}

void NodeBehaviourEditor::RenderVariableDetails(float height) {
    ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.0f, 1.0f), "VARIABLE DETAILS");
    ImGui::Spacing();

    NodeVariable* selVar = m_graph->FindVariableById(m_selectedVarId);
    if (!selVar) {
        ImGui::TextDisabled("Select a variable from the\nlist above to edit its details.");
        return;
    }

    // 1. Variable Name
    ImGui::Text("Name:");
    char nameBuf[64];
    strncpy(nameBuf, selVar->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##VarDetailName", nameBuf, sizeof(nameBuf))) {
        selVar->name = nameBuf;
        m_isDirty = true;
    }

    ImGui::Spacing();

    // 2. Variable Type Combo
    ImGui::Text("Type:");
    const char* types[] = { "Float", "Integer", "Boolean", "String", "Vector3", "GameObject" };
    NodePinType typeEnums[] = { NodePinType::Float, NodePinType::Int, NodePinType::Bool, NodePinType::String, NodePinType::Vector3, NodePinType::GameObject };
    int currentTypeIdx = 0;
    for (int i = 0; i < 6; ++i) {
        if (selVar->type == typeEnums[i]) { currentTypeIdx = i; break; }
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::Combo("##VarDetailType", &currentTypeIdx, types, IM_ARRAYSIZE(types))) {
        selVar->type = typeEnums[currentTypeIdx];
        m_isDirty = true;
    }

    ImGui::Spacing();

    // 3. Default Value
    ImGui::Text("Default Value:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (selVar->type == NodePinType::Float) {
        if (ImGui::DragFloat("##VarDefFloat", &selVar->floatValue, 0.1f)) m_isDirty = true;
    } else if (selVar->type == NodePinType::Int) {
        if (ImGui::DragInt("##VarDefInt", &selVar->intValue, 1)) m_isDirty = true;
    } else if (selVar->type == NodePinType::Bool) {
        if (ImGui::Checkbox("True##VarDefBool", &selVar->boolValue)) m_isDirty = true;
    } else if (selVar->type == NodePinType::String) {
        char sBuf[256];
        strncpy(sBuf, selVar->stringValue.c_str(), sizeof(sBuf) - 1);
        sBuf[sizeof(sBuf) - 1] = '\0';
        if (ImGui::InputText("##VarDefStr", sBuf, sizeof(sBuf))) {
            selVar->stringValue = sBuf;
            m_isDirty = true;
        }
    } else if (selVar->type == NodePinType::Vector3) {
        if (ImGui::DragFloat3("##VarDefVec3", &selVar->vectorValue.x, 0.1f)) m_isDirty = true;
    } else if (selVar->type == NodePinType::GameObject) {
        std::string preview = "(Self / None)";
        if (m_scene && selVar->targetObjectId != -1) {
            GameObject* target = m_scene->FindObject(selVar->targetObjectId);
            if (target) preview = target->name + " (ID: " + std::to_string(target->id) + ")";
            else preview = "Missing Object (ID: " + std::to_string(selVar->targetObjectId) + ")";
        }
        if (ImGui::BeginCombo("##VarDefObj", preview.c_str())) {
            if (ImGui::Selectable("(Self / None)", selVar->targetObjectId == -1)) {
                selVar->targetObjectId = -1;
                m_isDirty = true;
            }
            if (m_scene) {
                ImGui::Separator();
                for (const auto& sceneObj : m_scene->objects) {
                    bool isSel = (selVar->targetObjectId == sceneObj.id);
                    std::string itemLabel = sceneObj.name + " (ID: " + std::to_string(sceneObj.id) + ")";
                    if (ImGui::Selectable(itemLabel.c_str(), isSel)) {
                        selVar->targetObjectId = sceneObj.id;
                        m_isDirty = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    // 4. Instance Editable (Public flag)
    if (ImGui::Checkbox("Instance Editable", &selVar->isPublic)) {
        m_isDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Exposes this variable in the Details panel when attached to a GameObject.");
    }
}

void NodeBehaviourEditor::RenderSearchPopup() {
    ImGui::SetNextWindowPos(m_searchPopupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 380.0f), ImGuiCond_Appearing);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.12f, 0.14f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.24f, 0.28f, 1.0f));

    if (ImGui::BeginPopup("NodeActionSearchPopup")) {
        ImGui::TextColored(ImVec4(0.20f, 0.75f, 1.0f, 1.0f), "Add Node / Action");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_focusSearchInput) {
            ImGui::SetKeyboardFocusHere();
            m_focusSearchInput = false;
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##NodeSearchFilter", "Search nodes (e.g. Print, Event)...", m_searchFilterBuf, sizeof(m_searchFilterBuf));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        std::string filter = m_searchFilterBuf;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        auto Matches = [&](const std::string& text) {
            if (filter.empty()) return true;
            std::string lower = text;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower.find(filter) != std::string::npos;
        };

        ImVec2 canvasSpawnPos = ed::ScreenToCanvas(m_searchPopupPos);

        // Category 1: Utilities / Actions (Prominently displaying Print Node)
        bool showPrint = Matches("print") || Matches("string") || Matches("log") || Matches("dev") || Matches("util") || Matches("action");
        if (showPrint) {
            if (ImGui::TreeNodeEx("Utilities / Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Selectable("Print String (Print)")) {
                    m_graph->CreatePrintNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Prints a message to the screen HUD and engine output log.");
                }
                ImGui::TreePop();
            }
        }

        // Category 2: Transform (GameObject Properties)
        bool showTransform = Matches("transform") || Matches("location") || Matches("rotation") || Matches("scale") || Matches("relative") || Matches("relativelocation") || Matches("getrelativelocation") || Matches("setrelativelocation");
        if (showTransform) {
            if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                if ((Matches("Get Relative Location") || Matches("location")) && ImGui::Selectable("Get Relative Location")) {
                    m_graph->CreateGetRelativeLocationNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gets the Relative Location of target GameObject (or Self).");

                if ((Matches("Set Relative Location") || Matches("location")) && ImGui::Selectable("Set Relative Location")) {
                    m_graph->CreateSetRelativeLocationNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets the Relative Location of target GameObject (or Self).");

                if ((Matches("Get Relative Rotation") || Matches("rotation")) && ImGui::Selectable("Get Relative Rotation")) {
                    m_graph->CreateGetRelativeRotationNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gets the Relative Rotation of target GameObject (or Self).");

                if ((Matches("Set Relative Rotation") || Matches("rotation")) && ImGui::Selectable("Set Relative Rotation")) {
                    m_graph->CreateSetRelativeRotationNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets the Relative Rotation of target GameObject (or Self).");

                if ((Matches("Get Relative Scale") || Matches("scale")) && ImGui::Selectable("Get Relative Scale")) {
                    m_graph->CreateGetRelativeScaleNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gets the Relative Scale of target GameObject (or Self).");

                if ((Matches("Set Relative Scale") || Matches("scale")) && ImGui::Selectable("Set Relative Scale")) {
                    m_graph->CreateSetRelativeScaleNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets the Relative Scale of target GameObject (or Self).");

                ImGui::TreePop();
            }
        }

        // Category 3: Events
        bool showEvents = Matches("event") || Matches("start") || Matches("update") || Matches("fixed");
        if (showEvents) {
            if (ImGui::TreeNodeEx("Events", ImGuiTreeNodeFlags_DefaultOpen)) {
                if ((Matches("Event Start") || Matches("start")) && ImGui::Selectable("Event Start")) {
                    m_graph->CreateEventStartNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Called once when the game starts.");

                if ((Matches("Event Update") || Matches("update")) && ImGui::Selectable("Event Update")) {
                    m_graph->CreateEventUpdateNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Called every frame with Delta Seconds.");

                if ((Matches("Event FixedUpdate") || Matches("fixed")) && ImGui::Selectable("Event FixedUpdate")) {
                    m_graph->CreateEventFixedUpdateNode(canvasSpawnPos);
                    m_isDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Called at fixed physics timesteps.");

                ImGui::TreePop();
            }
        }

        // Category 3: Variables
        if (!m_graph->variables.empty() && (Matches("var") || Matches("get") || Matches("set") || Matches("variable"))) {
            if (ImGui::TreeNodeEx("Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& v : m_graph->variables) {
                    std::string getLabel = "Get " + v.name;
                    std::string setLabel = "Set " + v.name;
                    if (Matches(getLabel) && ImGui::Selectable(getLabel.c_str())) {
                        m_graph->CreateVariableGetNode(v.name, canvasSpawnPos);
                        m_isDirty = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (v.type != NodePinType::GameObject) {
                        if (Matches(setLabel) && ImGui::Selectable(setLabel.c_str())) {
                            m_graph->CreateVariableSetNode(v.name, canvasSpawnPos);
                            m_isDirty = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::TreePop();
            }
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

} // namespace NodeBehaviour
} // namespace Eunoia
