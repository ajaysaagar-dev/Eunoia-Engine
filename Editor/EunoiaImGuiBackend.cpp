#include "stdafx.h"
#include "EunoiaImGuiBackend.h"
#include "EunoiaTheme.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/ImGuizmo.h"
#include "wiShaderCompiler.h"

using namespace wi::graphics;

namespace EunoiaUI
{
	namespace
	{
		bool s_initialized = false;
		HWND s_hWnd = nullptr;
		Shader s_imguiVS;
		Shader s_imguiPS;
		Texture s_fontTexture;
		Sampler s_sampler;
		InputLayout s_inputLayout;
		PipelineState s_pipelineState;

		struct ImGuiConstants
		{
			float mvp[4][4];
		};

		bool CompileOrLoadShader(ShaderStage stage, Shader& shader, const std::string& name)
		{
			GraphicsDevice* device = wi::graphics::GetDevice();
			if (!device) return false;

			// 1. Try loading precompiled .cso from disk without registering in engine loader
			std::string csoName = name + ".cso";
			std::string csoPaths[] = {
				csoName,
				"Editor/" + csoName,
				wi::helper::GetCurrentPath() + "/" + csoName,
				wi::helper::GetCurrentPath() + "/Editor/" + csoName
			};
			for (const auto& path : csoPaths)
			{
				if (wi::helper::FileExists(path))
				{
					wi::vector<uint8_t> bytecode;
					if (wi::helper::FileRead(path, bytecode) && !bytecode.empty())
					{
						return device->CreateShader(stage, bytecode.data(), bytecode.size(), &shader, "main");
					}
				}
			}

			// 2. Try compiling directly from HLSL source
			std::string hlslName = name + ".hlsl";
			std::string candidatePaths[] = {
				hlslName,
				"Editor/" + hlslName,
				wi::helper::GetCurrentPath() + "/" + hlslName,
				wi::helper::GetCurrentPath() + "/Editor/" + hlslName,
				"Samples/Example_ImGui_Docking/" + hlslName
			};


			std::string foundPath = "";
			for (const auto& path : candidatePaths)
			{
				if (wi::helper::FileExists(path))
				{
					foundPath = path;
					break;
				}
			}

			if (foundPath.empty())
			{
				wi::backlog::post("[EunoiaUI] Could not find shader source: " + hlslName, wi::backlog::LogLevel::Error);
				return false;
			}

			wi::shadercompiler::CompilerInput input;
			input.format = device->GetShaderFormat();
			input.stage = stage;
			input.minshadermodel = ShaderModel::SM_5_0;
			input.entrypoint = "main";
			input.shadersourcefilename = foundPath;

			wi::shadercompiler::CompilerOutput output;
			wi::shadercompiler::Compile(input, output);

			if (output.IsValid() && output.shaderdata && output.shadersize > 0)
			{
				return device->CreateShader(stage, output.shaderdata, output.shadersize, &shader, "main");
			}

			wi::backlog::post("[EunoiaUI] Failed to compile shader: " + foundPath + " -> " + output.error_message, wi::backlog::LogLevel::Error);
			return false;
		}

		bool CreateDeviceObjects()
		{
			GraphicsDevice* device = wi::graphics::GetDevice();
			if (!device) return false;

			// Build font atlas
			ImGuiIO& io = ImGui::GetIO();
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

			// Create texture on device
			TextureDesc texDesc;
			texDesc.width = width;
			texDesc.height = height;
			texDesc.mip_levels = 1;
			texDesc.array_size = 1;
			texDesc.format = Format::R8G8B8A8_UNORM;
			texDesc.bind_flags = BindFlag::SHADER_RESOURCE;

			SubresourceData subData;
			subData.data_ptr = pixels;
			subData.row_pitch = width * GetFormatStride(texDesc.format);
			subData.slice_pitch = subData.row_pitch * height;

			if (!device->CreateTexture(&texDesc, &subData, &s_fontTexture))
			{
				wi::backlog::post("[EunoiaUI] Failed to create font atlas texture!", wi::backlog::LogLevel::Error);
				return false;
			}

			// Create Sampler
			SamplerDesc samplerDesc;
			samplerDesc.address_u = TextureAddressMode::WRAP;
			samplerDesc.address_v = TextureAddressMode::WRAP;
			samplerDesc.address_w = TextureAddressMode::WRAP;
			samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
			if (!device->CreateSampler(&samplerDesc, &s_sampler))
			{
				return false;
			}

			io.Fonts->SetTexID((ImTextureID)&s_fontTexture);

			// Input Layout matching ImDrawVert
			s_inputLayout.elements = {
				{ "POSITION", 0, Format::R32G32_FLOAT, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, pos), InputClassification::PER_VERTEX_DATA },
				{ "TEXCOORD", 0, Format::R32G32_FLOAT, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, uv), InputClassification::PER_VERTEX_DATA },
				{ "COLOR", 0, Format::R8G8B8A8_UNORM, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, col), InputClassification::PER_VERTEX_DATA }
			};

			// Pipeline State
			PipelineStateDesc psoDesc;
			psoDesc.vs = &s_imguiVS;
			psoDesc.ps = &s_imguiPS;
			psoDesc.il = &s_inputLayout;
			psoDesc.dss = wi::renderer::GetDepthStencilState(wi::enums::DSSTYPE_DEPTHDISABLED);
			psoDesc.rs = wi::renderer::GetRasterizerState(wi::enums::RSTYPE_DOUBLESIDED);
			psoDesc.bs = wi::renderer::GetBlendState(wi::enums::BSTYPE_TRANSPARENT);
			psoDesc.pt = PrimitiveTopology::TRIANGLELIST;

			return device->CreatePipelineState(&psoDesc, &s_pipelineState);
		}
	}

	bool ImGuiBackend::Initialize(HWND window)
	{
		if (s_initialized) return true;
		s_hWnd = window;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

		// Load Roboto font if available
		const char* fontCandidates[] = {
			"Roboto-Medium.ttf",
			"Editor/Roboto-Medium.ttf",
			"Samples/Example_ImGui_Docking/Roboto-Medium.ttf"
		};
		for (const auto& path : fontCandidates)
		{
			if (wi::helper::FileExists(path))
			{
				io.Fonts->AddFontFromFileTTF(path, 16.0f);
				break;
			}
		}

		// Apply Eunoia theme
		SetupTheme();

		// Init Win32 platform backend
		if (!ImGui_ImplWin32_Init(window))
		{
			wi::backlog::post("[EunoiaUI] Failed to init ImGui Win32 platform!", wi::backlog::LogLevel::Error);
			return false;
		}

		// Compile shaders
		if (!CompileOrLoadShader(ShaderStage::VS, s_imguiVS, "ImGuiVS"))
		{
			wi::backlog::post("[EunoiaUI] Failed to load ImGuiVS!", wi::backlog::LogLevel::Error);
			return false;
		}
		if (!CompileOrLoadShader(ShaderStage::PS, s_imguiPS, "ImGuiPS"))
		{
			wi::backlog::post("[EunoiaUI] Failed to load ImGuiPS!", wi::backlog::LogLevel::Error);
			return false;
		}

		if (!CreateDeviceObjects())
		{
			wi::backlog::post("[EunoiaUI] Failed to create device objects!", wi::backlog::LogLevel::Error);
			return false;
		}

		s_initialized = true;
		wi::backlog::post("[EunoiaUI] ImGui Backend initialized successfully.", wi::backlog::LogLevel::Default);
		return true;
	}

	void ImGuiBackend::Shutdown()
	{
		if (!s_initialized) return;
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		s_initialized = false;
	}

	void ImGuiBackend::NewFrame()
	{
		if (!s_initialized) return;

		if (!s_fontTexture.IsValid())
		{
			CreateDeviceObjects();
		}

		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiBackend::RenderDrawData(ImDrawData* drawData, CommandList cmd)
	{
		if (!s_initialized || !drawData || drawData->TotalVtxCount == 0) return;

		int fb_width = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		int fb_height = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fb_width <= 0 || fb_height <= 0) return;

		GraphicsDevice* device = wi::graphics::GetDevice();
		if (!device) return;

		// Allocate GPU memory for vertex & index buffers
		const uint64_t vbSize = sizeof(ImDrawVert) * drawData->TotalVtxCount;
		const uint64_t ibSize = sizeof(ImDrawIdx) * drawData->TotalIdxCount;
		auto vbAlloc = device->AllocateGPU(vbSize, cmd);
		auto ibAlloc = device->AllocateGPU(ibSize, cmd);

		ImDrawVert* vMem = reinterpret_cast<ImDrawVert*>(vbAlloc.data);
		ImDrawIdx* iMem = reinterpret_cast<ImDrawIdx*>(ibAlloc.data);
		for (int n = 0; n < drawData->CmdListsCount; ++n)
		{
			const ImDrawList* cmdList = drawData->CmdLists[n];
			memcpy(vMem, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(iMem, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
			vMem += cmdList->VtxBuffer.Size;
			iMem += cmdList->IdxBuffer.Size;
		}

		// Orthographic projection matrix
		const float L = drawData->DisplayPos.x;
		const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
		const float T = drawData->DisplayPos.y;
		const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

		ImGuiConstants constants;
		float mvp[4][4] = {
			{ 2.0f / (R - L),    0.0f,            0.0f, 0.0f },
			{ 0.0f,          2.0f / (T - B),      0.0f, 0.0f },
			{ 0.0f,          0.0f,            0.5f, 0.0f },
			{ (R + L)/(L - R), (T + B)/(B - T), 0.5f, 1.0f }
		};
		memcpy(&constants.mvp, mvp, sizeof(mvp));
		device->BindDynamicConstantBuffer(constants, 0, cmd);

		const GPUBuffer* vbs[] = { &vbAlloc.buffer };
		const uint32_t strides[] = { sizeof(ImDrawVert) };
		const uint64_t offsets[] = { vbAlloc.offset };
		device->BindVertexBuffers(vbs, 0, 1, strides, offsets, cmd);
		device->BindIndexBuffer(&ibAlloc.buffer, IndexBufferFormat::UINT16, ibAlloc.offset, cmd);

		Viewport vp;
		vp.width = (float)fb_width;
		vp.height = (float)fb_height;
		device->BindViewports(1, &vp, cmd);

		device->BindPipelineState(&s_pipelineState, cmd);
		device->BindSampler(&s_sampler, 0, cmd);

		ImVec2 clip_off = drawData->DisplayPos;
		int32_t vertexOffset = 0;
		uint32_t indexOffset = 0;

		for (int n = 0; n < drawData->CmdListsCount; ++n)
		{
			const ImDrawList* cmdList = drawData->CmdLists[n];
			for (int cmd_i = 0; cmd_i < cmdList->CmdBuffer.Size; ++cmd_i)
			{
				const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(cmdList, pcmd);
				}
				else
				{
					ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
					ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
					if (clip_max.x < clip_min.x || clip_max.y < clip_min.y) continue;

					Rect scissor;
					scissor.left = (int32_t)clip_min.x;
					scissor.top = (int32_t)clip_min.y;
					scissor.right = (int32_t)clip_max.x;
					scissor.bottom = (int32_t)clip_max.y;
					device->BindScissorRects(1, &scissor, cmd);

					const Texture* texture = (const Texture*)pcmd->TextureId;
					if (texture)
					{
						device->BindResource(texture, 0, cmd);
					}
					else
					{
						device->BindResource(&s_fontTexture, 0, cmd);
					}

					device->DrawIndexed(pcmd->ElemCount, indexOffset, vertexOffset, cmd);
				}
				indexOffset += pcmd->ElemCount;
			}
			vertexOffset += cmdList->VtxBuffer.Size;
		}

		// Restore full scissor
		Rect fullScissor;
		fullScissor.left = 0;
		fullScissor.top = 0;
		fullScissor.right = (int32_t)vp.width;
		fullScissor.bottom = (int32_t)vp.height;
		device->BindScissorRects(1, &fullScissor, cmd);
	}

	bool ImGuiBackend::IsInitialized() { return s_initialized; }
	bool ImGuiBackend::WantCaptureMouse() { return s_initialized && ImGui::GetIO().WantCaptureMouse; }
	bool ImGuiBackend::WantCaptureKeyboard() { return s_initialized && ImGui::GetIO().WantCaptureKeyboard; }

	ImTextureID ImGuiBackend::GetTextureID(const Texture* texture)
	{
		return (ImTextureID)texture;
	}
}
