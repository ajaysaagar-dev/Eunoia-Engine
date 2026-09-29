#pragma once
// ============================================================================
// Visual-Behaviuors Plugin — Node Graph Canvas Editor (ImGui / ImDrawList)
// Self-contained window that renders a full .ebehaviour graph editor.
// ============================================================================

#include "BehaviourGraph.h"
#include "BehaviourSerializer.h"
#include "NodeLibrary.h"

#include <imgui.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace VisualBehaviuors {

// ─────────────────────────────────────────────────────────────────────────────
// Pin-color mapping for data types
// ─────────────────────────────────────────────────────────────────────────────
inline ImU32 PinColor(PinDataType dt) {
    switch (dt) {
        case PinDataType::Exec:    return IM_COL32(255,255,255,255);
        case PinDataType::Bool:    return IM_COL32(220, 48, 48,255);
        case PinDataType::Int:     return IM_COL32( 64,200,220,255);
        case PinDataType::Float:   return IM_COL32( 60,210, 80,255);
        case PinDataType::String:  return IM_COL32(200, 80,200,255);
        case PinDataType::Vector3: return IM_COL32(220,210, 40,255);
        default:                   return IM_COL32(200,200,200,255);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Category header colors
// ─────────────────────────────────────────────────────────────────────────────
inline ImU32 CategoryColor(const std::string& cat) {
    if (cat == "Events")    return IM_COL32(130, 20, 20, 230);
    if (cat == "Flow")      return IM_COL32( 20, 60,130, 230);
    if (cat == "Debug")     return IM_COL32( 20,110, 80, 230);
    if (cat == "Math")      return IM_COL32( 60, 20,120, 230);
    if (cat == "Variables") return IM_COL32( 20, 90,130, 230);
    return IM_COL32(50,50,60,230);
}

// ─────────────────────────────────────────────────────────────────────────────
// BehaviourEditor — one open .ebehaviour document
// ─────────────────────────────────────────────────────────────────────────────
class BehaviourEditor {
public:
    BehaviourGraph graph;
    std::string    filePath;
    bool           isDirty       = false;
    bool           isOpen        = true;

    // Canvas state
    float          zoom           = 1.0f;
    ImVec2         pan            = {0.0f, 0.0f};

    // Interaction state
    bool           isDraggingCanvas   = false;
    ImVec2         dragStart          = {0.0f, 0.0f};
    ImVec2         dragPanStart       = {0.0f, 0.0f};

    NodeId         draggingNode       = 0;
    ImVec2         dragNodeOffset     = {0.0f, 0.0f};

    // Link creation
    bool           isLinkDragging     = false;
    NodeId         linkFromNode       = 0;
    PinId          linkFromPin        = 0;
    bool           linkFromIsOutput   = false;

    // Selection
    std::unordered_set<NodeId> selected;
    bool           isBoxSelecting     = false;
    ImVec2         boxSelectStart     = {0.0f, 0.0f};

    // Palette popup
    bool           showPalette        = false;
    char           paletteSearch[128] = {};
    ImVec2         paletteSpawnPos    = {0.0f, 0.0f};
    PinId          paletteConnectPin  = 0;
    NodeId         paletteConnectNode = 0;
    bool           paletteConnectIsOut = false;

    // ── Variable panel state ─────────────────────────────────────────────────
    char           newVarName[64]     = "MyVar";
    int            newVarType         = 0; // index into VarType enum
    uint64_t       editingVarId       = 0;

    // Selected variable in sidebar
    uint64_t       selectedVarId      = 0;

    // ── Selected node for Properties panel ───────────────────────────────────
    NodeId         selectedNodeForProps = 0;

    // ────────────────────────────────────────────────────────────────────────
    explicit BehaviourEditor(const std::string& path)
        : filePath(path)
    {
        zoom  = graph.zoom;
        pan.x = graph.panX;
        pan.y = graph.panY;
        if (std::filesystem::exists(path)) {
            LoadGraph(graph, path);
            zoom  = graph.zoom;
            pan.x = graph.panX;
            pan.y = graph.panY;
        }
    }

    // ── Save ─────────────────────────────────────────────────────────────────
    bool Save() {
        graph.panX = pan.x;
        graph.panY = pan.y;
        graph.zoom = zoom;
        if (SaveGraph(graph, filePath)) {
            isDirty = false;
            return true;
        }
        return false;
    }

    // ── Canvas helpers ────────────────────────────────────────────────────────
    ImVec2 CanvasToScreen(ImVec2 origin, float x, float y) const {
        return { origin.x + (x + pan.x) * zoom,
                 origin.y + (y + pan.y) * zoom };
    }
    ImVec2 ScreenToCanvas(ImVec2 origin, ImVec2 screen) const {
        return { (screen.x - origin.x) / zoom - pan.x,
                 (screen.y - origin.y) / zoom - pan.y };
    }

    // ── Zoom-to-fit ───────────────────────────────────────────────────────────
    void ZoomToFit(ImVec2 canvasSize) {
        if (graph.nodes.empty()) return;

        // Compute bounding box
        float minX =  1e9f, minY =  1e9f;
        float maxX = -1e9f, maxY = -1e9f;
        for (const auto& n : graph.nodes) {
            bool inSel = selected.empty() || selected.count(n.id);
            if (!inSel) continue;
            minX = std::min(minX, n.posX);
            minY = std::min(minY, n.posY);
            maxX = std::max(maxX, n.posX + n.width);
            maxY = std::max(maxY, n.posY + n.height);
        }
        float pad = 60.0f;
        float bw = maxX - minX + pad * 2;
        float bh = maxY - minY + pad * 2;
        float zx = canvasSize.x / bw;
        float zy = canvasSize.y / bh;
        zoom = std::max(0.25f, std::min(2.0f, std::min(zx, zy)));
        pan.x = -minX + pad + (canvasSize.x / zoom - bw) * 0.5f;
        pan.y = -minY + pad + (canvasSize.y / zoom - bh) * 0.5f;
    }

    // ────────────────────────────────────────────────────────────────────────
    // Render — call every frame inside your ImGui window
    // ────────────────────────────────────────────────────────────────────────
    void Render() {
        // ── Toolbar ──────────────────────────────────────────────────────────
        if (ImGui::Button("💾 Save")) {
            Save();
        }
        ImGui::SameLine();
        if (ImGui::Button("⚙ Compile")) {
            ImGui::OpenPopup("CompileStub##vb");
        }
        if (ImGui::BeginPopup("CompileStub##vb")) {
            ImGui::TextDisabled("Compile not implemented.");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (ImGui::Button("⌖ Zoom Fit (F)")) {
            ImVec2 cs = {avail.x, avail.y - 24.0f};
            ZoomToFit(cs);
        }
        ImGui::SameLine();
        ImGui::Text("  %.0f%%", zoom * 100.0f);
        if (isDirty) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.6f,0.2f,1), "  *"); }

        ImGui::Separator();

        // ── Main layout: Canvas | Right sidebar ──────────────────────────────
        float sidebarW   = 240.0f;
        float canvasW    = ImGui::GetContentRegionAvail().x - sidebarW - 6.0f;
        float canvasH    = ImGui::GetContentRegionAvail().y;

        // ── Canvas region ─────────────────────────────────────────────────────
        ImGui::BeginChild("##VBCanvas", {canvasW, canvasH}, false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderCanvas();
        ImGui::EndChild();

        ImGui::SameLine();

        // ── Right sidebar ─────────────────────────────────────────────────────
        ImGui::BeginChild("##VBSidebar", {sidebarW, canvasH}, true);
        RenderSidebar();
        ImGui::EndChild();
    }

private:
    // ── Sidebar (Variables + Properties) ─────────────────────────────────────
    void RenderSidebar() {
        if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
            // List
            for (auto& v : graph.variables) {
                bool isSel = (selectedVarId == v.id);
                if (ImGui::Selectable(v.name.c_str(), isSel))
                    selectedVarId = v.id;
                // Drag into canvas → spawns Get node
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("VB_VAR_GET", &v.id, sizeof(uint64_t));
                    ImGui::Text("Get %s", v.name.c_str());
                    ImGui::EndDragDropSource();
                }
                // Alt+drag → Set node
                if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(0) && ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("VB_VAR_SET", &v.id, sizeof(uint64_t));
                        ImGui::Text("Set %s", v.name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", VarTypeToString(v.type));
            }
            ImGui::Separator();
            // Add new variable
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputText("##NewVarName", newVarName, sizeof(newVarName));
            ImGui::SameLine();
            const char* varTypes[] = {"Bool","Int","Float","String","Vec3"};
            ImGui::SetNextItemWidth(60.0f);
            ImGui::Combo("##NewVarType", &newVarType, varTypes, 5);
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Var")) {
                std::string vn = newVarName;
                if (!vn.empty() && graph.IsVariableNameUnique(vn)) {
                    graph.AddVariable(vn, (VarType)newVarType);
                    isDirty = true;
                }
            }
            // Delete selected var
            if (selectedVarId != 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("✕")) {
                    graph.RemoveVariable(selectedVarId);
                    selectedVarId = 0;
                    isDirty = true;
                }
                // Rename
                Variable* sv = graph.FindVariable(selectedVarId);
                if (sv) {
                    static char renBuf[64] = {};
                    if (ImGui::IsItemActivated()) strncpy(renBuf, sv->name.c_str(), 63);
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::InputText("Rename##vbren", renBuf, sizeof(renBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (strlen(renBuf) > 0 && graph.IsVariableNameUnique(renBuf, selectedVarId)) {
                            graph.RenameVariable(selectedVarId, renBuf);
                            isDirty = true;
                        }
                    }
                }
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            // If a node is selected, show its properties
            Node* selNode = selectedNodeForProps ? graph.FindNode(selectedNodeForProps) : nullptr;
            Variable* selVar = selectedVarId ? graph.FindVariable(selectedVarId) : nullptr;

            if (selNode) {
                const NodeTypeDef* def = NodeRegistry::Get().Find(selNode->typeId);
                ImGui::Text("Node: %s", def ? def->displayName.c_str() : selNode->typeId.c_str());
                ImGui::Text("ID:   %llu", (unsigned long long)selNode->id);
                ImGui::Text("Category: %s", def ? def->category.c_str() : "-");
                ImGui::Separator();
                ImGui::Text("Position:");
                ImGui::DragFloat("X##npx", &selNode->posX, 1.0f); isDirty |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Y##npy", &selNode->posY, 1.0f); isDirty |= ImGui::IsItemDeactivatedAfterEdit();
                if (!selNode->params.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Params:");
                    for (auto& kv : selNode->params) {
                        char buf[256];
                        strncpy(buf, kv.second.c_str(), 255);
                        if (ImGui::InputText(kv.first.c_str(), buf, sizeof(buf)))
                            kv.second = buf, isDirty = true;
                    }
                }
                static char commentBuf[256] = {};
                strncpy(commentBuf, selNode->comment.c_str(), 255);
                ImGui::Text("Comment:");
                if (ImGui::InputText("##nodecomment", commentBuf, sizeof(commentBuf)))
                    selNode->comment = commentBuf, isDirty = true;
            } else if (selVar) {
                ImGui::Text("Variable: %s", selVar->name.c_str());
                ImGui::Text("Type: %s", VarTypeToString(selVar->type));
                ImGui::Separator();
                char defBuf[128];
                strncpy(defBuf, selVar->defaultValue.c_str(), 127);
                if (ImGui::InputText("Default", defBuf, sizeof(defBuf)))
                    selVar->defaultValue = defBuf, isDirty = true;
            } else {
                ImGui::Text("Behaviour: %s", graph.name.c_str());
                ImGui::Separator();
                ImGui::Text("Nodes:     %d", (int)graph.nodes.size());
                ImGui::Text("Links:     %d", (int)graph.links.size());
                ImGui::Text("Variables: %d", (int)graph.variables.size());
            }
        }
    }

    // ── Node canvas ──────────────────────────────────────────────────────────
    void RenderCanvas() {
        ImDrawList* dl      = ImGui::GetWindowDrawList();
        ImVec2      cOrigin = ImGui::GetCursorScreenPos();
        ImVec2      cSize   = ImGui::GetContentRegionAvail();

        // Clipping
        dl->PushClipRect(cOrigin, {cOrigin.x + cSize.x, cOrigin.y + cSize.y}, true);

        // ── Grid ─────────────────────────────────────────────────────────────
        DrawGrid(dl, cOrigin, cSize);

        // ── Links ─────────────────────────────────────────────────────────────
        DrawLinks(dl, cOrigin);

        // ── Live link preview ─────────────────────────────────────────────────
        if (isLinkDragging) {
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 fromPos  = GetPinScreenPos(cOrigin, linkFromNode, linkFromPin);
            dl->AddBezierCubic(fromPos,
                {fromPos.x + 60 * zoom, fromPos.y},
                {mousePos.x - 60 * zoom, mousePos.y},
                mousePos,
                IM_COL32(255,255,255,180), 2.0f * zoom);
        }

        // ── Box select ────────────────────────────────────────────────────────
        if (isBoxSelecting) {
            ImVec2 mp = ImGui::GetMousePos();
            ImVec2 a  = {std::min(boxSelectStart.x, mp.x), std::min(boxSelectStart.y, mp.y)};
            ImVec2 b  = {std::max(boxSelectStart.x, mp.x), std::max(boxSelectStart.y, mp.y)};
            dl->AddRectFilled(a, b, IM_COL32(100,140,255,40));
            dl->AddRect(a, b, IM_COL32(100,140,255,220), 0.0f, 0, 1.5f);
        }

        // ── Nodes ─────────────────────────────────────────────────────────────
        for (auto& n : graph.nodes) {
            DrawNode(dl, cOrigin, n);
        }

        dl->PopClipRect();

        // ── Invisible button for canvas input ─────────────────────────────────
        ImGui::SetCursorScreenPos(cOrigin);
        ImGui::InvisibleButton("##vbcanvas", cSize,
            ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);
        bool isHovered = ImGui::IsItemHovered();

        HandleCanvasInput(cOrigin, cSize, isHovered);

        // ── Node palette popup ────────────────────────────────────────────────
        if (showPalette) {
            ImGui::OpenPopup("##VBPalette");
            showPalette = false;
        }
        if (ImGui::BeginPopup("##VBPalette")) {
            DrawPalette(cOrigin);
            ImGui::EndPopup();
        }

        // ── Keyboard shortcuts (only when canvas is focused) ─────────────────
        if (isHovered || ImGui::IsWindowFocused()) {
            // F = Zoom to fit
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                ZoomToFit(cSize);
            }
            // Delete = remove selected nodes
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selected.empty()) {
                for (NodeId nid : selected) graph.RemoveNode(nid);
                selected.clear();
                isDirty = true;
            }
            // Ctrl+D = duplicate
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_D) && !selected.empty()) {
                DuplicateSelected();
                isDirty = true;
            }
            // Ctrl+S = save
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S)) {
                Save();
            }
        }

        // Drag-and-drop: variable into canvas → Get/Set node
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VB_VAR_GET")) {
                uint64_t vid = *(const uint64_t*)pl->Data;
                Variable* v = graph.FindVariable(vid);
                if (v) {
                    ImVec2 dropPos = ScreenToCanvas(cOrigin, ImGui::GetMousePos());
                    NodeId nid = graph.AddNode("Var.Get", dropPos.x, dropPos.y);
                    Node* n = graph.FindNode(nid);
                    if (n) n->params["varName"] = v->name;
                    isDirty = true;
                }
            }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VB_VAR_SET")) {
                uint64_t vid = *(const uint64_t*)pl->Data;
                Variable* v = graph.FindVariable(vid);
                if (v) {
                    ImVec2 dropPos = ScreenToCanvas(cOrigin, ImGui::GetMousePos());
                    NodeId nid = graph.AddNode("Var.Set", dropPos.x, dropPos.y);
                    Node* n = graph.FindNode(nid);
                    if (n) n->params["varName"] = v->name;
                    isDirty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ── Grid rendering ────────────────────────────────────────────────────────
    void DrawGrid(ImDrawList* dl, ImVec2 origin, ImVec2 size) {
        ImU32 colMinor = IM_COL32(55,55,60,180);
        ImU32 colMajor = IM_COL32(70,70,80,220);

        float gridMinor = 20.0f * zoom;
        float gridMajor = 100.0f * zoom;

        float offX = fmod(pan.x * zoom, gridMinor);
        float offY = fmod(pan.y * zoom, gridMinor);

        // Minor lines
        for (float x = offX; x < size.x; x += gridMinor)
            dl->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + size.y}, colMinor, 0.5f);
        for (float y = offY; y < size.y; y += gridMinor)
            dl->AddLine({origin.x, origin.y + y}, {origin.x + size.x, origin.y + y}, colMinor, 0.5f);

        // Major lines
        float offMX = fmod(pan.x * zoom, gridMajor);
        float offMY = fmod(pan.y * zoom, gridMajor);
        for (float x = offMX; x < size.x; x += gridMajor)
            dl->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + size.y}, colMajor, 1.0f);
        for (float y = offMY; y < size.y; y += gridMajor)
            dl->AddLine({origin.x, origin.y + y}, {origin.x + size.x, origin.y + y}, colMajor, 1.0f);
    }

    // ── Links rendering ───────────────────────────────────────────────────────
    void DrawLinks(ImDrawList* dl, ImVec2 origin) {
        for (const auto& lk : graph.links) {
            ImVec2 from = GetPinScreenPos(origin, lk.fromNode, lk.fromPin);
            ImVec2 to   = GetPinScreenPos(origin, lk.toNode,   lk.toPin);
            float dx = std::abs(to.x - from.x) * 0.5f + 40.0f * zoom;
            ImU32 col = IM_COL32(200,200,200,220);
            // Color by pin data type
            const Node* fn = nullptr;
            for (const auto& n : graph.nodes) if (n.id == lk.fromNode) fn = &n;
            if (fn) {
                for (const auto& p : fn->pins)
                    if (p.id == lk.fromPin) { col = PinColor(p.dataType); break; }
            }
            dl->AddBezierCubic(from, {from.x+dx,from.y}, {to.x-dx,to.y}, to, col, 2.5f * zoom);

            // Right-click to break
            ImVec2 mid = { (from.x+to.x)*0.5f, (from.y+to.y)*0.5f };
            float breakRadius = 6.0f * zoom;
            ImVec2 mp = ImGui::GetMousePos();
            float dxm = mp.x - mid.x, dym = mp.y - mid.y;
            if (dxm*dxm + dym*dym < breakRadius*breakRadius) {
                dl->AddCircleFilled(mid, breakRadius, IM_COL32(220,80,80,200));
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    graph.RemoveLink(lk.linkId);
                    isDirty = true;
                    break; // iterator invalidated
                }
            }
        }
    }

    // ── Single node rendering ─────────────────────────────────────────────────
    void DrawNode(ImDrawList* dl, ImVec2 origin, Node& n) {
        const NodeTypeDef* def = NodeRegistry::Get().Find(n.typeId);
        std::string displayName = def ? def->displayName : n.typeId;
        // Override display for variable Get/Set
        auto vit = n.params.find("varName");
        if (vit != n.params.end() && !vit->second.empty()) {
            if (n.typeId == "Var.Get") displayName = "Get " + vit->second;
            if (n.typeId == "Var.Set") displayName = "Set " + vit->second;
        }

        float headerH = 22.0f * zoom;
        ImVec2 pos = CanvasToScreen(origin, n.posX, n.posY);
        ImVec2 sz  = {n.width * zoom, n.height * zoom};
        bool isSel = selected.count(n.id) > 0;

        // Background
        ImU32 bgCol   = IM_COL32(38,38,44,240);
        ImU32 bordCol = isSel ? IM_COL32(100,160,255,255) : IM_COL32(80,80,90,200);
        dl->AddRectFilled(pos, {pos.x+sz.x, pos.y+sz.y}, bgCol, 6.0f * zoom);
        dl->AddRect(pos, {pos.x+sz.x, pos.y+sz.y}, bordCol, 6.0f * zoom, 0, isSel ? 2.0f : 1.0f);

        // Header
        ImU32 hdrCol = def ? CategoryColor(def->category) : IM_COL32(50,50,60,230);
        dl->AddRectFilled(pos, {pos.x+sz.x, pos.y+headerH}, hdrCol, 6.0f * zoom);
        float fontSize = ImGui::GetFontSize() * zoom;
        dl->AddText(nullptr, fontSize, {pos.x + 6.0f*zoom, pos.y + 3.0f*zoom},
                    IM_COL32(255,255,255,255), displayName.c_str());

        // Pins
        float pinR   = 5.0f * zoom;
        float pinStep = (sz.y - headerH) / (float)(std::max(1, (int)n.pins.size()) + 1);
        int   inIdx  = 0, outIdx = 0;
        float inputCount  = 0, outputCount = 0;
        for (const auto& p : n.pins) {
            if (p.direction == PinDirection::Input)  inputCount++;
            else                                      outputCount++;
        }
        float inStep  = (sz.y - headerH) / (inputCount  + 1);
        float outStep = (sz.y - headerH) / (outputCount + 1);

        for (auto& p : n.pins) {
            ImVec2 pinPos;
            if (p.direction == PinDirection::Input) {
                inIdx++;
                pinPos = {pos.x, pos.y + headerH + inStep * inIdx};
            } else {
                outIdx++;
                pinPos = {pos.x + sz.x, pos.y + headerH + outStep * outIdx};
            }

            ImU32 pc = PinColor(p.dataType);
            if (p.kind == PinKind::Exec) {
                // Exec: triangle
                float ts = pinR * 1.2f;
                ImVec2 tri[3];
                if (p.direction == PinDirection::Input) {
                    tri[0] = {pinPos.x - ts, pinPos.y - ts};
                    tri[1] = {pinPos.x + ts, pinPos.y};
                    tri[2] = {pinPos.x - ts, pinPos.y + ts};
                } else {
                    tri[0] = {pinPos.x + ts, pinPos.y - ts};
                    tri[1] = {pinPos.x - ts, pinPos.y};
                    tri[2] = {pinPos.x + ts, pinPos.y + ts};
                }
                dl->AddTriangleFilled(tri[0], tri[1], tri[2], pc);
            } else {
                dl->AddCircleFilled(pinPos, pinR, pc);
                dl->AddCircle(pinPos, pinR, IM_COL32(0,0,0,200), 0, 1.5f);
            }

            // Pin label
            float lblX = (p.direction == PinDirection::Input)
                         ? pinPos.x + pinR + 3.0f*zoom
                         : pinPos.x - pinR - 3.0f*zoom - ImGui::CalcTextSize(p.name.c_str()).x * zoom;
            dl->AddText(nullptr, ImGui::GetFontSize() * zoom * 0.85f,
                {lblX, pinPos.y - ImGui::GetFontSize()*zoom*0.85f*0.5f},
                IM_COL32(220,220,220,220), p.name.c_str());

            // Hit-test: mouse hover → start/end link dragging
            ImVec2 mp = ImGui::GetMousePos();
            float dx2 = mp.x - pinPos.x, dy2 = mp.y - pinPos.y;
            bool pinHovered = (dx2*dx2 + dy2*dy2 < (pinR*2)*(pinR*2));

            if (pinHovered) {
                dl->AddCircle(pinPos, pinR + 3.0f*zoom, IM_COL32(255,255,255,200), 0, 1.5f);
                bool isOut = (p.direction == PinDirection::Output);

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isLinkDragging) {
                    isLinkDragging     = true;
                    linkFromNode       = n.id;
                    linkFromPin        = p.id;
                    linkFromIsOutput   = isOut;
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isLinkDragging && linkFromNode != n.id) {
                    // Attempt to connect
                    NodeId fn = isOut ? linkFromNode : n.id;
                    PinId  fp = isOut ? linkFromPin  : p.id;
                    NodeId tn = isOut ? n.id         : linkFromNode;
                    PinId  tp = isOut ? p.id         : linkFromPin;
                    if (graph.AddLink(fn, fp, tn, tp)) isDirty = true;
                    isLinkDragging = false;
                }

                // Alt+click = break pin links
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    graph.BreakPinLinks(p.id);
                    isDirty = true;
                }
            }
        }

        // ── Node dragging ──────────────────────────────────────────────────────
        ImVec2 mp = ImGui::GetMousePos();
        bool overHeader = (mp.x >= pos.x && mp.x <= pos.x+sz.x && mp.y >= pos.y && mp.y <= pos.y+headerH);

        if (overHeader && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isLinkDragging) {
            if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !ImGui::IsKeyDown(ImGuiKey_LeftShift) && !selected.count(n.id))
                selected.clear();
            selected.insert(n.id);
            selectedNodeForProps = n.id;
            draggingNode = n.id;
            dragNodeOffset = {(mp.x - pos.x)/zoom, (mp.y - pos.y)/zoom};
        }

        if (draggingNode == n.id && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 newScreenPos = {mp.x - dragNodeOffset.x * zoom, mp.y - dragNodeOffset.y * zoom};
            ImVec2 newCanvasPos = ScreenToCanvas(origin, newScreenPos);
            n.posX = newCanvasPos.x;
            n.posY = newCanvasPos.y;
            // Move entire selection together
            if (selected.size() > 1 && selected.count(n.id)) {
                // offset applied to primary node, others follow relative
            }
            isDirty = true;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (draggingNode == n.id) draggingNode = 0;
        }

        // Right-click context menu on node
        if (overHeader && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !isLinkDragging) {
            selected.insert(n.id);
            ImGui::OpenPopup(("NodeCtx##" + std::to_string(n.id)).c_str());
        }
        if (ImGui::BeginPopup(("NodeCtx##" + std::to_string(n.id)).c_str())) {
            ImGui::Text("%s", displayName.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("🗑 Delete Node")) {
                graph.RemoveNode(n.id);
                selected.erase(n.id);
                if (selectedNodeForProps == n.id) selectedNodeForProps = 0;
                isDirty = true;
                ImGui::EndPopup();
                return; // iterator invalid
            }
            if (ImGui::MenuItem("📋 Duplicate")) {
                std::unordered_set<NodeId> prev = selected;
                selected.clear();
                selected.insert(n.id);
                DuplicateSelected();
                selected = prev;
                isDirty = true;
            }
            ImGui::EndPopup();
        }
    }

    // ── Canvas input handling (pan, zoom, box-select, palette) ───────────────
    void HandleCanvasInput(ImVec2 origin, ImVec2 size, bool isHovered) {
        ImVec2 mp = ImGui::GetMousePos();

        // Zoom via scroll
        if (isHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.01f) {
            float oldZoom = zoom;
            zoom += ImGui::GetIO().MouseWheel * 0.12f * zoom;
            zoom = std::max(0.25f, std::min(2.0f, zoom));
            // Zoom around cursor
            ImVec2 cvMouse = ScreenToCanvas(origin, mp);
            // adjust pan so cursor stays fixed
            pan.x = (mp.x - origin.x) / zoom - cvMouse.x;
            pan.y = (mp.y - origin.y) / zoom - cvMouse.y;
        }

        // Pan: MMB drag or RMB drag (with threshold)
        static ImVec2 s_rmbDragStart = {0,0};
        static bool   s_rmbWasDrag  = false;

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            isDraggingCanvas = true;
            dragStart        = mp;
            dragPanStart     = pan;
        }
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            s_rmbDragStart = mp;
            s_rmbWasDrag   = false;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f)) {
            s_rmbWasDrag = true;
            if (!isDraggingCanvas && isHovered) {
                isDraggingCanvas = true;
                dragStart        = s_rmbDragStart;
                dragPanStart     = pan;
            }
        }
        if (isDraggingCanvas) {
            bool btnDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (btnDown) {
                ImVec2 ref = (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) ? dragStart : s_rmbDragStart;
                pan.x = dragPanStart.x + (mp.x - ref.x) / zoom;
                pan.y = dragPanStart.y + (mp.y - ref.y) / zoom;
            } else {
                isDraggingCanvas = false;
            }
        }

        // Open palette on plain RMB click (no drag)
        if (isHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !isDraggingCanvas && !s_rmbWasDrag && !isLinkDragging) {
            showPalette         = true;
            paletteSpawnPos     = ScreenToCanvas(origin, mp);
            paletteConnectPin   = 0;
            paletteConnectNode  = 0;
        }

        // Cancel link drag
        if (isLinkDragging) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                // Released on empty space → open palette filtered by compatible pin
                if (isHovered) {
                    showPalette        = true;
                    paletteSpawnPos    = ScreenToCanvas(origin, mp);
                    paletteConnectPin  = linkFromPin;
                    paletteConnectNode = linkFromNode;
                    paletteConnectIsOut = linkFromIsOutput;
                }
                isLinkDragging = false;
            }
        }

        // Box select: LMB drag on empty canvas
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isLinkDragging && draggingNode == 0) {
            isBoxSelecting = true;
            boxSelectStart = mp;
            if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !ImGui::IsKeyDown(ImGuiKey_LeftShift))
                selected.clear();
        }
        if (isBoxSelecting) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                // Finalize box selection
                ImVec2 a = {std::min(boxSelectStart.x, mp.x), std::min(boxSelectStart.y, mp.y)};
                ImVec2 b = {std::max(boxSelectStart.x, mp.x), std::max(boxSelectStart.y, mp.y)};
                for (const auto& n : graph.nodes) {
                    ImVec2 ns = CanvasToScreen(origin, n.posX, n.posY);
                    ImVec2 ne = {ns.x + n.width*zoom, ns.y + n.height*zoom};
                    if (ns.x < b.x && ne.x > a.x && ns.y < b.y && ne.y > a.y)
                        selected.insert(n.id);
                }
                isBoxSelecting = false;
            }
        }
    }

    // ── Node palette ──────────────────────────────────────────────────────────
    void DrawPalette(ImVec2 canvasOrigin) {
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##palSearch", "🔍 Search nodes...", paletteSearch, sizeof(paletteSearch));
        ImGui::Separator();

        std::string searchLow = paletteSearch;
        std::transform(searchLow.begin(), searchLow.end(), searchLow.begin(), ::tolower);

        // Group by category
        std::unordered_map<std::string, std::vector<const NodeTypeDef*>> groups;
        for (const auto& kv : NodeRegistry::Get().All()) {
            std::string nameLow = kv.second.displayName;
            std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(), ::tolower);
            if (searchLow.empty() || nameLow.find(searchLow) != std::string::npos)
                groups[kv.second.category].push_back(&kv.second);
        }

        bool spawned = false;
        for (auto& [cat, defs] : groups) {
            if (ImGui::TreeNodeEx(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const NodeTypeDef* d : defs) {
                    if (ImGui::MenuItem(d->displayName.c_str())) {
                        NodeId nid = graph.AddNode(d->typeId, paletteSpawnPos.x, paletteSpawnPos.y);
                        // auto-connect if dragged from pin
                        if (nid && paletteConnectPin && paletteConnectNode) {
                            Node* newNode = graph.FindNode(nid);
                            if (newNode) {
                                for (auto& p : newNode->pins) {
                                    bool tryConnect = paletteConnectIsOut
                                        ? (p.direction == PinDirection::Input)
                                        : (p.direction == PinDirection::Output);
                                    if (tryConnect) {
                                        NodeId fn = paletteConnectIsOut ? paletteConnectNode : nid;
                                        PinId  fp = paletteConnectIsOut ? paletteConnectPin  : p.id;
                                        NodeId tn = paletteConnectIsOut ? nid  : paletteConnectNode;
                                        PinId  tp = paletteConnectIsOut ? p.id : paletteConnectPin;
                                        if (graph.AddLink(fn, fp, tn, tp)) break;
                                    }
                                }
                            }
                        }
                        isDirty = true;
                        spawned = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::TreePop();
            }
        }
    }

    // ── Duplication ───────────────────────────────────────────────────────────
    void DuplicateSelected() {
        std::vector<NodeId> toAdd;
        std::unordered_map<NodeId,NodeId> oldToNew;
        for (NodeId nid : selected) {
            Node* src = graph.FindNode(nid);
            if (!src) continue;
            NodeId newId = graph.AddNode(src->typeId, src->posX + 30.0f, src->posY + 30.0f);
            Node* dst = graph.FindNode(newId);
            if (dst) {
                dst->params   = src->params;
                dst->comment  = src->comment;
                oldToNew[nid] = newId;
                toAdd.push_back(newId);
            }
        }
        // Duplicate internal links
        for (const auto& lk : graph.links) {
            if (oldToNew.count(lk.fromNode) && oldToNew.count(lk.toNode)) {
                // Find matching pins by index
                Node* fn = graph.FindNode(oldToNew[lk.fromNode]);
                Node* tn = graph.FindNode(oldToNew[lk.toNode]);
                if (fn && tn && !fn->pins.empty() && !tn->pins.empty()) {
                    graph.AddLink(fn->id, fn->pins[0].id, tn->id, tn->pins[0].id);
                }
            }
        }
        selected.clear();
        for (NodeId nid : toAdd) selected.insert(nid);
    }

    // ── Get pin screen position ───────────────────────────────────────────────
    ImVec2 GetPinScreenPos(ImVec2 origin, NodeId nodeId, PinId pinId) const {
        for (const auto& n : graph.nodes) {
            if (n.id != nodeId) continue;
            ImVec2 pos = CanvasToScreen(origin, n.posX, n.posY);
            ImVec2 sz  = {n.width * zoom, n.height * zoom};
            float headerH = 22.0f * zoom;

            float inputCount = 0, outputCount = 0;
            for (const auto& p : n.pins) {
                if (p.direction == PinDirection::Input)  inputCount++;
                else                                      outputCount++;
            }
            int inIdx = 0, outIdx = 0;
            for (const auto& p : n.pins) {
                if (p.id == pinId) {
                    if (p.direction == PinDirection::Input) {
                        inIdx++;
                        float step = (sz.y - headerH) / (inputCount + 1);
                        return {pos.x, pos.y + headerH + step * inIdx};
                    } else {
                        outIdx++;
                        float step = (sz.y - headerH) / (outputCount + 1);
                        return {pos.x + sz.x, pos.y + headerH + step * outIdx};
                    }
                }
                if (p.direction == PinDirection::Input)  inIdx++;
                else                                      outIdx++;
            }
        }
        return {0,0};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// EditorManager — manages all open BehaviourEditor instances
// ─────────────────────────────────────────────────────────────────────────────
class BehaviourEditorManager {
public:
    static BehaviourEditorManager& Get() {
        static BehaviourEditorManager inst;
        return inst;
    }

    void Open(const std::string& filePath) {
        // If already open, focus it
        for (auto& e : editors) {
            if (e->filePath == filePath) { e->isOpen = true; return; }
        }
        editors.push_back(std::make_unique<BehaviourEditor>(filePath));
    }

    // Call each frame from the main editor loop
    void RenderAll() {
        for (auto& e : editors) {
            if (!e->isOpen) continue;
            std::string title = "🧩 " + e->graph.name;
            if (e->isDirty) title += " *";
            title += "##vb" + e->filePath;

            bool stillOpen = true;
            if (ImGui::Begin(title.c_str(), &stillOpen,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                e->Render();
            }
            ImGui::End();
            if (!stillOpen) {
                if (e->isDirty) {
                    // Prompt: save on close is handled by the dirty asterisk in title
                    // For simplicity, auto-save on close
                    e->Save();
                }
                e->isOpen = false;
            }
        }
        // Cleanup closed editors
        editors.erase(std::remove_if(editors.begin(), editors.end(),
            [](const std::unique_ptr<BehaviourEditor>& e){ return !e->isOpen; }),
            editors.end());
    }

    bool IsOpen(const std::string& filePath) const {
        for (const auto& e : editors) if (e->filePath == filePath && e->isOpen) return true;
        return false;
    }

private:
    std::vector<std::unique_ptr<BehaviourEditor>> editors;
};

} // namespace VisualBehaviuors
