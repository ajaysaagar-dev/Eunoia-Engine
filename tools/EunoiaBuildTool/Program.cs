using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;

namespace EunoiaBuildTool
{
    class Program
    {
        private static string s_projectDir = "";
        private static string s_buildRoot = "";
        private static string s_outputDir = "";
        private static string s_intermediatesDir = "";
        private static string s_projectsRoot = "";
        private static string s_customProjectPath = "";
        private static bool s_editorOnly = false;
        private static bool s_gameOnly = false;
        private static bool s_clean = false;
        private static bool s_verbose = false;

        private static string s_compilerExe = "";
        private static string s_windresExe = "";
        private static string s_cmakeExe = "";
        private static string s_ninjaExe = "";
        private static string s_resObj = "";

        private static List<string> s_projectScripts = new List<string>();
        private static Stopwatch s_totalTimer = new Stopwatch();

        static int Main(string[] args)
        {
            try
            {
                Console.OutputEncoding = Encoding.UTF8;
            }
            catch { }

            s_totalTimer.Start();

            // Locate project root
            string currentDir = AppDomain.CurrentDomain.BaseDirectory;
            s_projectDir = FindEngineRoot(currentDir);
            if (string.IsNullOrEmpty(s_projectDir))
            {
                s_projectDir = Directory.GetCurrentDirectory();
                s_projectDir = FindEngineRoot(s_projectDir);
                if (string.IsNullOrEmpty(s_projectDir))
                {
                    s_projectDir = Directory.GetCurrentDirectory();
                }
            }

            ParseArguments(args);

            s_buildRoot = Path.Combine(s_projectDir, "Build");
            s_outputDir = Path.Combine(s_buildRoot, "Engine");
            s_intermediatesDir = Path.Combine(s_buildRoot, "Intermediates");

            string parentProjects = Path.GetFullPath(Path.Combine(s_projectDir, "..", "Projects"));
            if (Directory.Exists(parentProjects))
            {
                s_projectsRoot = parentProjects;
            }
            else
            {
                s_projectsRoot = Path.Combine(s_projectDir, "Projects");
            }

            PrintBanner();

            if (s_clean)
            {
                CleanBuild();
                return 0;
            }

            EnsureDirectories();

            // Phase 1: Environment & Toolchains
            PrintSection(1, 5, "TOOLCHAIN DETECTION", ConsoleColor.Cyan);
            if (!DetectToolchains())
            {
                PrintError("Toolchain detection failed. Cannot proceed with build.");
                return 1;
            }

            // Phase 2: Asset & Script Discovery
            PrintSection(2, 5, "DISCOVERY & SCRIPT SCANNING", ConsoleColor.Cyan);
            ScanProjectScripts();

            // Phase 3: Application Resources
            PrintSection(3, 5, "RESOURCE COMPILATION", ConsoleColor.Cyan);
            CompileResources();

            // Phase 4: Eunoia-Editor (DX12 + ImGui)
            if (!s_gameOnly)
            {
                PrintSection(4, 5, "BUILDING EUNOIA-EDITOR (DX12)", ConsoleColor.Cyan);
                if (!BuildEditor())
                {
                    PrintError("Engine Editor build failed.");
                    return 1;
                }
                PackageEngine();
            }

            // Phase 5: Standalone Game Runtime
            if (!s_editorOnly)
            {
                PrintSection(5, 5, "BUILDING GAME RUNTIME", ConsoleColor.Cyan);
                if (!BuildGameRuntime())
                {
                    PrintWarning("Game runtime build encountered warnings or errors.");
                }
            }

            s_totalTimer.Stop();
            PrintSummary();
            return 0;
        }

        private static void ParseArguments(string[] args)
        {
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i].Trim();
                if (arg.Equals("--editor-only", StringComparison.OrdinalIgnoreCase) || arg.Equals("-e", StringComparison.OrdinalIgnoreCase))
                {
                    s_editorOnly = true;
                }
                else if (arg.Equals("--game-only", StringComparison.OrdinalIgnoreCase) || arg.Equals("-g", StringComparison.OrdinalIgnoreCase))
                {
                    s_gameOnly = true;
                }
                else if (arg.Equals("--clean", StringComparison.OrdinalIgnoreCase) || arg.Equals("-c", StringComparison.OrdinalIgnoreCase))
                {
                    s_clean = true;
                }
                else if (arg.Equals("--verbose", StringComparison.OrdinalIgnoreCase) || arg.Equals("-v", StringComparison.OrdinalIgnoreCase))
                {
                    s_verbose = true;
                }
                else if (arg.Equals("--project", StringComparison.OrdinalIgnoreCase) || arg.Equals("-p", StringComparison.OrdinalIgnoreCase))
                {
                    if (i + 1 < args.Length)
                    {
                        s_customProjectPath = args[++i];
                    }
                }
                else if (arg.Equals("--help", StringComparison.OrdinalIgnoreCase) || arg.Equals("-h", StringComparison.OrdinalIgnoreCase) || arg.Equals("/?"))
                {
                    PrintHelp();
                    Environment.Exit(0);
                }
                else if (!arg.StartsWith("-") && string.IsNullOrEmpty(s_customProjectPath) && Directory.Exists(arg))
                {
                    s_customProjectPath = arg;
                }
            }
        }

        private static void PrintHelp()
        {
            Console.WriteLine(@"
Eunoia Engine Build System (.NET)
Usage: Build.bat [options] [ProjectPath]

Options:
  -e, --editor-only       Build only the Eunoia Editor executable and package engine
  -g, --game-only         Build only the Standalone Game Runtime
  -p, --project <path>    Specify game project directory (defaults to Projects\test)
  -c, --clean             Clean all build intermediates and outputs
  -v, --verbose           Enable verbose compilation logs
  -h, --help              Show this help message
");
        }

        private static string FindEngineRoot(string startDir)
        {
            string dir = startDir;
            while (!string.IsNullOrEmpty(dir))
            {
                if (File.Exists(Path.Combine(dir, "Build.bat")) && Directory.Exists(Path.Combine(dir, "Engine")))
                {
                    return dir;
                }
                DirectoryInfo parent = Directory.GetParent(dir);
                if (parent == null) break;
                dir = parent.FullName;
            }
            return "";
        }

        private static void PrintBanner()
        {
            Console.ForegroundColor = ConsoleColor.Magenta;
            Console.WriteLine(@"
================================================================================
           ______ _   _ _   _  ____ _____       ______ _   _  _____ _____ _   _ _____ 
          |  ____| | | | \ | |/ __ \_   _|/\   |  ____| \ | |/ ____|_   _| \ | |  ____|
          | |__  | | | |  \| | |  | || | /  \  | |__  |  \| | |  __  | | |  \| | |__   
          |  __| | | | | . ` | |  | || |/ /\ \ |  __| | . ` | | |_ | | | | . ` |  __|  
          | |____| |_| | |\  | |__| || / ____ \| |____| |\  | |__| |_| |_| |\  | |____ 
          |______|\___/|_| \_|\____/_____/_/  \_\______|_| \_|\_____|_____|_| \_|______|
================================================================================
                                .NET BUILD SYSTEM
");
            Console.ResetColor();
            PrintInfo(string.Format("Engine Root:    {0}", s_projectDir));
            PrintInfo(string.Format("Build Output:   {0}", s_outputDir));
            PrintInfo(string.Format("Projects Root:  {0}", s_projectsRoot));
            Console.WriteLine();
        }

        private static void CleanBuild()
        {
            PrintSection(1, 1, "CLEANING BUILD ARTIFACTS", ConsoleColor.Yellow);
            try
            {
                if (Directory.Exists(s_buildRoot))
                {
                    PrintInfo(string.Format("Removing build directory: {0}", s_buildRoot));
                    Directory.Delete(s_buildRoot, true);
                    PrintSuccess("Build directory cleaned successfully.");
                }
                else
                {
                    PrintInfo("Build directory does not exist. Nothing to clean.");
                }
            }
            catch (Exception ex)
            {
                PrintError(string.Format("Failed to clean build directory: {0}", ex.Message));
            }
        }

        private static void EnsureDirectories()
        {
            string[] dirs = new string[]
            {
                s_buildRoot,
                s_outputDir,
                s_intermediatesDir,
                Path.Combine(s_buildRoot, "Tests"),
                Path.Combine(s_buildRoot, "Runtime"),
                s_projectsRoot
            };

            foreach (var d in dirs)
            {
                if (!Directory.Exists(d))
                {
                    Directory.CreateDirectory(d);
                }
            }
        }

        private static bool DetectToolchains()
        {
            // Compiler (g++ / cl)
            string localGpp = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "g++.exe");
            if (File.Exists(localGpp))
            {
                s_compilerExe = localGpp;
            }
            else
            {
                s_compilerExe = FindInPath("g++.exe");
                if (string.IsNullOrEmpty(s_compilerExe))
                {
                    s_compilerExe = FindInPath("cl.exe");
                }
            }

            if (string.IsNullOrEmpty(s_compilerExe))
            {
                PrintError("C++ Compiler not found! Ensure w64devkit or g++/cl is installed.");
                return false;
            }

            PrintSuccess(string.Format("Compiler:       {0}", s_compilerExe));

            // Windres (Resource compiler)
            string localWindres = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "windres.exe");
            if (File.Exists(localWindres))
            {
                s_windresExe = localWindres;
            }
            else
            {
                s_windresExe = FindInPath("windres.exe");
            }

            if (!string.IsNullOrEmpty(s_windresExe))
            {
                PrintSuccess(string.Format("Resource Comp:  {0}", s_windresExe));
            }

            // CMake
            string localCmake = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "cmake.exe");
            s_cmakeExe = File.Exists(localCmake) ? localCmake : FindInPath("cmake.exe");
            if (!string.IsNullOrEmpty(s_cmakeExe))
            {
                PrintSuccess(string.Format("CMake:          {0}", s_cmakeExe));
            }

            // Ninja
            string localNinja = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "ninja.exe");
            s_ninjaExe = File.Exists(localNinja) ? localNinja : FindInPath("ninja.exe");
            if (!string.IsNullOrEmpty(s_ninjaExe))
            {
                PrintSuccess(string.Format("Ninja:          {0}", s_ninjaExe));
            }

            return true;
        }

        private static string FindInPath(string exeName)
        {
            string pathEnv = Environment.GetEnvironmentVariable("PATH");
            if (string.IsNullOrEmpty(pathEnv)) return null;
            string[] paths = pathEnv.Split(';');
            foreach (string p in paths)
            {
                string clean = p.Trim('\"', ' ');
                if (string.IsNullOrEmpty(clean)) continue;
                string full = Path.Combine(clean, exeName);
                if (File.Exists(full)) return full;
            }
            return null;
        }

        private static void ScanProjectScripts()
        {
            s_projectScripts.Clear();
            if (Directory.Exists(s_projectsRoot))
            {
                foreach (string projDir in Directory.GetDirectories(s_projectsRoot))
                {
                    string contentDir = Path.Combine(projDir, "Content");
                    if (Directory.Exists(contentDir))
                    {
                        var cppFiles = Directory.GetFiles(contentDir, "*.cpp", SearchOption.AllDirectories);
                        foreach (var f in cppFiles)
                        {
                            string fileName = Path.GetFileName(f);
                            if (fileName.EndsWith(".assetmeta", StringComparison.OrdinalIgnoreCase) ||
                                fileName.EndsWith(".meta", StringComparison.OrdinalIgnoreCase))
                            {
                                continue;
                            }
                            s_projectScripts.Add(f);
                            PrintInfo(string.Format("Found Behaviour Script: {0} -> {1}", Path.GetFileName(projDir), fileName));
                        }
                    }
                }
            }

            PrintSuccess(string.Format("Total project behaviour scripts discovered: {0}", s_projectScripts.Count));
        }

        private static void CompileResources()
        {
            s_resObj = "";
            string rcFile = Path.Combine(s_projectDir, "Editor", "Editor", "Src", "Eunoia.rc");
            if (string.IsNullOrEmpty(s_windresExe) || !File.Exists(rcFile))
            {
                PrintInfo("Skipping Windows resource compilation (windres or Eunoia.rc not found).");
                return;
            }

            string outObj = Path.Combine(s_intermediatesDir, "Eunoia.res.o");
            string args = string.Format("-I\"{0}\" -i \"{1}\" -O coff -o \"{2}\"", s_projectDir, rcFile, outObj);

            bool success = RunProcessWithProgress("Compiling application icons & metadata...", s_windresExe, args);
            if (success && File.Exists(outObj))
            {
                s_resObj = outObj;
                PrintSuccess(string.Format("Resource object compiled: {0}", Path.GetFileName(outObj)));
            }
            else
            {
                PrintWarning("Resource compilation produced warnings or failed. Proceeding without resource object.");
            }
        }

        private static bool BuildEditor()
        {
            KillProcess("Eunoia-Editor.exe");

            string editorExe = Path.Combine(s_outputDir, "Eunoia-Editor.exe");

            List<string> includes = new List<string>
            {
                Path.Combine(s_projectDir, "Engine", "EngineCore", "Include"),
                Path.Combine(s_projectDir, "Engine", "EnginePlatform", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineRenderer", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Include"),
                Path.Combine(s_projectDir, "Engine", "EunoiaPluginCore", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Mesh-Cluster-Culling", "Include"),
                Path.Combine(s_projectDir, "Plugins", "water", "include"),
                Path.Combine(s_projectDir, "Plugins", "skybox", "include"),
                Path.Combine(s_projectDir, "Plugins", "Lights-Baking", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Visual-Behaviuors", "Include"),
                Path.Combine(s_projectDir, "Plugins", "UI-System", "Include"),
                Path.Combine(s_projectDir, "Editor", "Editor", "Include"),
                Path.Combine(s_projectDir, "deps", "tinyobj"),
                Path.Combine(s_projectDir, "deps", "cgltf"),
                Path.Combine(s_projectDir, "deps", "ufbx"),
                Path.Combine(s_projectDir, "deps", "imgui"),
                Path.Combine(s_projectDir, "deps", "imgui", "backends"),
                Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "include"),
                Path.Combine(s_projectDir, "deps", "glm"),
                Path.Combine(s_projectDir, "deps", "json"),
                s_projectDir
            };

            List<string> sources = new List<string>
            {
                Path.Combine(s_projectDir, "deps", "ufbx", "ufbx.c"),
                Path.Combine(s_projectDir, "Plugins", "Mesh-Cluster-Culling", "Source", "MeshClusterBuilder.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Mesh-Cluster-Culling", "Source", "MeshClusterCulling.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Mesh-Cluster-Culling", "Source", "HiZManager.cpp"),
                Path.Combine(s_projectDir, "Plugins", "water", "src", "WaterSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "water", "src", "WaterPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Lights-Baking", "Source", "LightBakeManager.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Lights-Baking", "Source", "LightsBakingPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Source", "GraphicsSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Source", "GraphicsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Visual-Behaviuors", "Source", "VisualBehaviuorsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UI-System", "Source", "UISystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UI-System", "Source", "UIPlugin.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "MeshImporter.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "TextureManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "DirectXTex.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "DirectXTexD3D12.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetRegistry.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Src", "EunoiaBehaviour.cpp"),
                Path.Combine(s_projectDir, "Engine", "EnginePlatform", "Src", "Window.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "Device.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "SwapChain.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "CommandContext.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "DescriptorAllocator.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "Fence.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "PipelineState.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Src", "UploadHeap.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineRenderer", "Src", "SceneRenderer.cpp"),
                Path.Combine(s_projectDir, "Editor", "Editor", "Src", "Main.cpp"),
                Path.Combine(s_projectDir, "Editor", "Editor", "Src", "EngineUI.cpp"),
                Path.Combine(s_projectDir, "Editor", "Editor", "Src", "EditorGizmoSystem.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "imgui.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "imgui_draw.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "imgui_tables.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "imgui_widgets.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "backends", "imgui_impl_glfw.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "backends", "imgui_impl_dx12.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "backends", "imgui_impl_opengl3.cpp"),
                Path.Combine(s_projectDir, "deps", "imgui", "ImGuizmo.cpp")
            };

            // Add user behaviour scripts
            sources.AddRange(s_projectScripts);

            StringBuilder sb = new StringBuilder();
            sb.Append("-std=c++20 -O2 ");
            foreach (var inc in includes)
            {
                sb.Append(string.Format("-I\"{0}\" ", inc));
            }
            foreach (var src in sources)
            {
                sb.Append(string.Format("\"{0}\" ", src));
            }

            string libDir = Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64");
            sb.Append(string.Format("-L\"{0}\" ", libDir));
            sb.Append("-lglfw3 -ld3d12 -ldxgi -ld3dcompiler -lgdi32 -limm32 -lcomdlg32 -lshell32 -lole32 -lopengl32 -ldwmapi ");

            if (!string.IsNullOrEmpty(s_resObj))
            {
                sb.Append(string.Format("\"{0}\" ", s_resObj));
            }

            sb.Append(string.Format("-o \"{0}\"", editorExe));

            bool success = RunProcessWithProgress("Compiling Eunoia-Editor (DirectX 12 + ImGui)...", s_compilerExe, sb.ToString());
            if (success && File.Exists(editorExe))
            {
                FileInfo fi = new FileInfo(editorExe);
                PrintSuccess(string.Format("Eunoia-Editor.exe built successfully! ({0:F2} MB)", fi.Length / (1024.0 * 1024.0)));
                return true;
            }
            return false;
        }

        private static void PackageEngine()
        {
            PrintInfo("Packaging engine binaries, libraries, shaders, and resources...");

            // Copy DLLs
            CopyFileIfExists(Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64", "glfw3.dll"), Path.Combine(s_outputDir, "glfw3.dll"));
            CopyFileIfExists(Path.Combine(s_projectDir, "Plugins", "primitives", "primitives.dll"), Path.Combine(s_outputDir, "primitives.dll"));

            // Sync directories
            SyncDirectory(Path.Combine(s_projectDir, "Resources"), Path.Combine(s_outputDir, "Resources"));
            SyncDirectory(Path.Combine(s_projectDir, "Shaders"), Path.Combine(s_outputDir, "Shaders"));
            SyncDirectory(Path.Combine(s_projectDir, "Engine"), Path.Combine(s_outputDir, "Engine"));
            SyncDirectory(Path.Combine(s_projectDir, "Editor"), Path.Combine(s_outputDir, "Editor"));
            SyncDirectory(Path.Combine(s_projectDir, "Runtime"), Path.Combine(s_outputDir, "Runtime"));
            SyncDirectory(Path.Combine(s_projectDir, "Docs"), Path.Combine(s_outputDir, "Docs"));
            SyncDirectory(Path.Combine(s_projectDir, "Plugins"), Path.Combine(s_outputDir, "Plugins"));
            SyncDirectory(Path.Combine(s_projectDir, "Projects"), Path.Combine(s_outputDir, "Projects"));
            SyncDirectory(Path.Combine(s_projectDir, "deps"), Path.Combine(s_outputDir, "deps"));
            SyncDirectory(Path.Combine(s_projectDir, "tools"), Path.Combine(s_outputDir, "tools"));

            // Create Run.bat launcher
            string runBat = Path.Combine(s_outputDir, "Run.bat");
            File.WriteAllText(runBat, "@echo off\r\ncd /d \"%~dp0\"\r\necho [INFO] Starting Eunoia-Editor [DirectX 12]...\r\nstart \"\" \"Eunoia-Editor.exe\"\r\n");

            PrintSuccess("Complete engine distribution packaged into: " + s_outputDir);
        }

        private static bool BuildGameRuntime()
        {
            string targetProj = s_customProjectPath;
            if (string.IsNullOrEmpty(targetProj))
            {
                targetProj = Path.Combine(s_projectsRoot, "test");
            }
            if (!Directory.Exists(targetProj))
            {
                targetProj = Path.Combine(s_projectsRoot, "Horror-Thriller");
            }

            string projName = Path.GetFileName(targetProj);
            string gameBuildDir = Path.Combine(targetProj, "Build");
            if (!Directory.Exists(gameBuildDir)) Directory.CreateDirectory(gameBuildDir);

            KillProcess("test.exe");
            KillProcess(string.Format("{0}.exe", projName));

            string gameExe = Path.Combine(gameBuildDir, "test.exe");

            List<string> includes = new List<string>
            {
                Path.Combine(s_projectDir, "Engine", "EngineCore", "Include"),
                Path.Combine(s_projectDir, "Engine", "EnginePlatform", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineRHI", "Include"),
                Path.Combine(s_projectDir, "Engine", "EngineRenderer", "Include"),
                Path.Combine(s_projectDir, "Engine", "EunoiaPluginCore", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Lights-Baking", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Visual-Behaviuors", "Include"),
                Path.Combine(s_projectDir, "Plugins", "UI-System", "Include"),
                Path.Combine(s_projectDir, "deps", "tinyobj"),
                Path.Combine(s_projectDir, "deps", "cgltf"),
                Path.Combine(s_projectDir, "deps", "ufbx"),
                Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "include"),
                Path.Combine(s_projectDir, "deps", "glm"),
                Path.Combine(s_projectDir, "deps", "json"),
                s_projectDir
            };

            List<string> sources = new List<string>
            {
                Path.Combine(s_projectDir, "deps", "ufbx", "ufbx.c"),
                Path.Combine(s_projectDir, "Plugins", "UI-System", "Source", "UISystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UI-System", "Source", "UIPlugin.cpp"),
                Path.Combine(s_projectDir, "Engine", "EnginePlatform", "Src", "Window.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Src", "EunoiaBehaviour.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetRegistry.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "TextureManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "DirectXTex.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "MeshImporter.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Lights-Baking", "Source", "LightBakeManager.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Source", "GraphicsSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Source", "GraphicsPlugin.cpp"),
                Path.Combine(s_projectDir, "Runtime", "Src", "Main.cpp")
            };

            // Add project specific scripts
            string contentDir = Path.Combine(targetProj, "Content");
            if (Directory.Exists(contentDir))
            {
                foreach (var f in Directory.GetFiles(contentDir, "*.cpp", SearchOption.AllDirectories))
                {
                    if (!f.EndsWith(".assetmeta", StringComparison.OrdinalIgnoreCase) &&
                        !f.EndsWith(".meta", StringComparison.OrdinalIgnoreCase))
                    {
                        sources.Add(f);
                    }
                }
            }

            StringBuilder sb = new StringBuilder();
            sb.Append("-std=c++20 -O2 ");
            foreach (var inc in includes) sb.Append(string.Format("-I\"{0}\" ", inc));
            foreach (var src in sources) sb.Append(string.Format("\"{0}\" ", src));

            string libDir = Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64");
            sb.Append(string.Format("-L\"{0}\" ", libDir));
            sb.Append("-lglfw3 -lgdi32 -lole32 -lshell32 ");

            if (!string.IsNullOrEmpty(s_resObj)) sb.Append(string.Format("\"{0}\" ", s_resObj));

            sb.Append(string.Format("-o \"{0}\"", gameExe));

            bool success = RunProcessWithProgress(string.Format("Compiling Standalone Game Runtime ({0})...", projName), s_compilerExe, sb.ToString());
            if (success && File.Exists(gameExe))
            {
                // Copy to project name executable
                string projExe = Path.Combine(gameBuildDir, string.Format("{0}.exe", projName));
                try { File.Copy(gameExe, projExe, true); } catch { }

                // Package Game Assets & DLLs
                CopyFileIfExists(Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64", "glfw3.dll"), Path.Combine(gameBuildDir, "glfw3.dll"));
                CopyFileIfExists(Path.Combine(s_projectDir, "Plugins", "primitives", "primitives.dll"), Path.Combine(gameBuildDir, "primitives.dll"));

                SyncDirectory(Path.Combine(s_projectDir, "Shaders"), Path.Combine(gameBuildDir, "Shaders"));
                SyncDirectory(Path.Combine(s_projectDir, "Resources"), Path.Combine(gameBuildDir, "Resources"));

                if (Directory.Exists(Path.Combine(targetProj, "Cooked"))) SyncDirectory(Path.Combine(targetProj, "Cooked"), Path.Combine(gameBuildDir, "Cooked"));
                if (Directory.Exists(Path.Combine(targetProj, "Content"))) SyncDirectory(Path.Combine(targetProj, "Content"), Path.Combine(gameBuildDir, "Content"));
                if (Directory.Exists(Path.Combine(targetProj, "Scenes"))) SyncDirectory(Path.Combine(targetProj, "Scenes"), Path.Combine(gameBuildDir, "Scenes"));
                if (Directory.Exists(Path.Combine(targetProj, "Materials"))) SyncDirectory(Path.Combine(targetProj, "Materials"), Path.Combine(gameBuildDir, "Materials"));
                if (Directory.Exists(Path.Combine(targetProj, "Models"))) SyncDirectory(Path.Combine(targetProj, "Models"), Path.Combine(gameBuildDir, "Models"));

                // Write Game Run.bat
                string gameRunBat = Path.Combine(gameBuildDir, "Run.bat");
                File.WriteAllText(gameRunBat, string.Format("@echo off\r\ncd /d \"%~dp0\"\r\necho [INFO] Starting {0}...\r\nstart \"\" \"test.exe\"\r\n", projName));

                PrintSuccess(string.Format("Game runtime packaged successfully into: {0}", gameBuildDir));
                return true;
            }
            return false;
        }

        private static bool RunProcessWithProgress(string taskName, string exePath, string args)
        {
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.White;
            Console.Write("  [STAGE] ");
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(taskName);
            Console.ResetColor();

            Stopwatch sw = Stopwatch.StartNew();
            List<string> errorLines = new List<string>();
            List<string> warningLines = new List<string>();

            ProcessStartInfo psi = new ProcessStartInfo
            {
                FileName = exePath,
                Arguments = args,
                WorkingDirectory = s_projectDir,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            bool isDone = false;
            char[] spinner = new char[] { '|', '/', '-', '\\' };
            int spinIndex = 0;

            Thread progressThread = new Thread(() =>
            {
                while (!isDone)
                {
                    double elapsed = sw.Elapsed.TotalSeconds;
                    int percent = Math.Min(95, (int)(elapsed * 3.5)); // Simulated smooth progress
                    string bar = GenerateProgressBar(percent, 24);

                    try
                    {
                        Console.CursorLeft = 0;
                        Console.ForegroundColor = ConsoleColor.DarkGray;
                        Console.Write("    [");
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.Write(spinner[spinIndex % spinner.Length]);
                        Console.ForegroundColor = ConsoleColor.DarkGray;
                        Console.Write("] ");
                        Console.ForegroundColor = ConsoleColor.Yellow;
                        Console.Write(bar);
                        Console.ForegroundColor = ConsoleColor.White;
                        Console.Write(string.Format(" {0:F1}s  ", elapsed));
                        Console.ResetColor();
                    }
                    catch { }

                    spinIndex++;
                    Thread.Sleep(100);
                }
            });

            using (Process p = new Process { StartInfo = psi })
            {
                p.OutputDataReceived += (s, e) =>
                {
                    if (!string.IsNullOrWhiteSpace(e.Data))
                    {
                        string line = e.Data.Trim();
                        if (line.IndexOf("error:", StringComparison.OrdinalIgnoreCase) >= 0)
                            errorLines.Add(line);
                        else if (line.IndexOf("warning:", StringComparison.OrdinalIgnoreCase) >= 0)
                            warningLines.Add(line);

                        if (s_verbose)
                        {
                            Console.WriteLine(string.Format("\r\n    [OUT] {0}", line));
                        }
                    }
                };

                p.ErrorDataReceived += (s, e) =>
                {
                    if (!string.IsNullOrWhiteSpace(e.Data))
                    {
                        string line = e.Data.Trim();
                        if (line.IndexOf("error:", StringComparison.OrdinalIgnoreCase) >= 0)
                            errorLines.Add(line);
                        else if (line.IndexOf("warning:", StringComparison.OrdinalIgnoreCase) >= 0)
                            warningLines.Add(line);

                        if (s_verbose)
                        {
                            Console.WriteLine(string.Format("\r\n    [ERR] {0}", line));
                        }
                    }
                };

                p.Start();
                progressThread.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                p.WaitForExit();
                isDone = true;
                progressThread.Join();
                sw.Stop();

                try
                {
                    Console.CursorLeft = 0;
                    string fullBar = GenerateProgressBar(100, 24);
                    Console.ForegroundColor = ConsoleColor.DarkGray;
                    Console.Write("    [");
                    Console.ForegroundColor = p.ExitCode == 0 ? ConsoleColor.Green : ConsoleColor.Red;
                    Console.Write(p.ExitCode == 0 ? "OK" : "XX");
                    Console.ForegroundColor = ConsoleColor.DarkGray;
                    Console.Write("] ");
                    Console.ForegroundColor = p.ExitCode == 0 ? ConsoleColor.Green : ConsoleColor.Red;
                    Console.Write(fullBar);
                    Console.ForegroundColor = ConsoleColor.White;
                    Console.WriteLine(string.Format(" {0:F1}s  ", sw.Elapsed.TotalSeconds));
                    Console.ResetColor();
                }
                catch { }

                if (warningLines.Count > 0 && !s_verbose)
                {
                    PrintWarning(string.Format("Encountered {0} compiler warning(s):", warningLines.Count));
                    foreach (var w in warningLines.Take(3))
                    {
                        Console.ForegroundColor = ConsoleColor.DarkYellow;
                        Console.WriteLine(string.Format("      -> {0}", w));
                        Console.ResetColor();
                    }
                    if (warningLines.Count > 3)
                    {
                        Console.ForegroundColor = ConsoleColor.DarkGray;
                        Console.WriteLine(string.Format("      -> ... and {0} more warnings.", warningLines.Count - 3));
                        Console.ResetColor();
                    }
                }

                if (errorLines.Count > 0)
                {
                    PrintError(string.Format("Encountered {0} compilation error(s):", errorLines.Count));
                    foreach (var err in errorLines)
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine(string.Format("      [ERROR] {0}", err));
                        Console.ResetColor();
                    }
                }

                return p.ExitCode == 0;
            }
        }

        private static string GenerateProgressBar(int percent, int totalWidth)
        {
            percent = Math.Max(0, Math.Min(100, percent));
            int filled = (percent * totalWidth) / 100;
            int empty = totalWidth - filled;
            return "[" + new string('=', filled) + new string('-', empty) + string.Format("] {0,3}%", percent);
        }

        private static void SyncDirectory(string sourceDir, string targetDir)
        {
            if (!Directory.Exists(sourceDir)) return;
            if (!Directory.Exists(targetDir)) Directory.CreateDirectory(targetDir);

            foreach (string file in Directory.GetFiles(sourceDir))
            {
                string dest = Path.Combine(targetDir, Path.GetFileName(file));
                try
                {
                    if (!File.Exists(dest) || File.GetLastWriteTimeUtc(file) > File.GetLastWriteTimeUtc(dest))
                    {
                        File.Copy(file, dest, true);
                    }
                }
                catch { }
            }

            foreach (string dir in Directory.GetDirectories(sourceDir))
            {
                SyncDirectory(dir, Path.Combine(targetDir, Path.GetFileName(dir)));
            }
        }

        private static void CopyFileIfExists(string src, string dst)
        {
            try
            {
                if (File.Exists(src))
                {
                    string dir = Path.GetDirectoryName(dst);
                    if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
                    File.Copy(src, dst, true);
                }
            }
            catch { }
        }

        private static void KillProcess(string processName)
        {
            try
            {
                string procNoExt = Path.GetFileNameWithoutExtension(processName);
                foreach (var p in Process.GetProcessesByName(procNoExt))
                {
                    p.Kill();
                    p.WaitForExit(1000);
                }
            }
            catch { }
        }

        private static void PrintSection(int step, int total, string title, ConsoleColor color)
        {
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.DarkGray;
            Console.Write(string.Format("[{0}/{1}] ", step, total));
            Console.ForegroundColor = color;
            Console.WriteLine(string.Format("=== {0} ===", title));
            Console.ResetColor();
        }

        private static void PrintInfo(string msg)
        {
            Console.ForegroundColor = ConsoleColor.DarkCyan;
            Console.Write("  [INFO] ");
            Console.ForegroundColor = ConsoleColor.Gray;
            Console.WriteLine(msg);
            Console.ResetColor();
        }

        private static void PrintSuccess(string msg)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.Write("  [SUCCESS] ");
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine(msg);
            Console.ResetColor();
        }

        private static void PrintWarning(string msg)
        {
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.Write("  [WARN] ");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine(msg);
            Console.ResetColor();
        }

        private static void PrintError(string msg)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.Write("  [ERROR] ");
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine(msg);
            Console.ResetColor();
        }

        private static void PrintSummary()
        {
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine(@"
+------------------------------------------------------------------------------+
|                         BUILD COMPLETED SUCCESSFULLY                         |
+------------------------------+-----------------------------------------------+");
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine(string.Format("| Total Elapsed Time           | {0:F2} seconds", s_totalTimer.Elapsed.TotalSeconds).PadRight(79) + "|");
            Console.WriteLine(string.Format("| Engine Output                | {0}", Path.Combine(s_buildRoot, "Engine")).PadRight(79) + "|");
            Console.WriteLine(string.Format("| Editor Executable            | Eunoia-Editor.exe").PadRight(79) + "|");
            Console.WriteLine(string.Format("| Discovered Scripts           | {0} scripts", s_projectScripts.Count).PadRight(79) + "|");
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine(@"+------------------------------+-----------------------------------------------+");
            Console.ResetColor();
            Console.WriteLine();
        }
    }
}
