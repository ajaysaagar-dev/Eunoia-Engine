using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EunoiaBuildTool
{
    class Program
    {
        private static string s_projectDir = "";
        private static string s_buildRoot = "";
        private static string s_outputDir = "";
        private static string s_intermediatesDir = "";
        private static string s_objDir = "";
        private static string s_projectsRoot = "";
        private static string s_customProjectPath = "";
        private static string s_customBuildRoot = "";
        private static bool s_buildGame = false;
        private static bool s_clean = false;
        private static bool s_verbose = false;
        private static bool s_watch = false;

        private static string s_compilerExe = "";
        private static string s_gccExe = "";
        private static string s_ccacheExe = "";
        private static string s_windresExe = "";
        private static string s_cmakeExe = "";
        private static string s_ninjaExe = "";
        private static string s_resObj = "";

        private static int s_maxConcurrency = Environment.ProcessorCount;
        private static List<string> s_projectScripts = new List<string>();
        private static Stopwatch s_totalTimer = new Stopwatch();

        static int Main(string[] args)
        {
            try { Console.OutputEncoding = Encoding.UTF8; } catch { }
            s_totalTimer.Start();

            // Locate engine root
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

            if (!string.IsNullOrEmpty(s_customBuildRoot))
            {
                s_buildRoot = Path.GetFullPath(s_customBuildRoot);
            }
            else
            {
                // Default build folder unconditionally to C:\Projects\Eunoia-Engine\Build
                s_buildRoot = Path.GetFullPath(Path.Combine(s_projectDir, "..", "Build"));
            }

            s_outputDir = Path.Combine(s_buildRoot, "Engine");
            s_intermediatesDir = Path.Combine(s_buildRoot, "Intermediates");
            s_objDir = Path.Combine(s_intermediatesDir, "Obj");

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

            // Interactive prompt if run interactively with no explicit build flags
            bool isInteractiveClean = false;
            if (args.Length == 0 && !s_clean && !s_watch)
            {
                PromptBuildModeSelection();
                if (s_clean) isInteractiveClean = true;
            }

            // Terminate running instances
            KillProcess("Eunoia-Editor.exe");
            KillProcess("Eunoia-Game.exe");

            if (s_clean)
            {
                CleanBuild();
                // If CLI passed --clean explicitly without interactive prompt, exit here.
                // If interactive clean build was chosen, proceed to build from clean state.
                if (!isInteractiveClean)
                {
                    return 0;
                }
            }

            EnsureDirectories();

            // Phase 1: Environment & Multi-core Toolchain Detection
            PrintSection(1, 4, "TOOLCHAIN & PARALLEL ENVIRONMENT", ConsoleColor.Cyan);
            if (!DetectToolchains())
            {
                PrintError("Toolchain detection failed. Cannot proceed with build.");
                return 1;
            }

            if (s_watch)
            {
                RunWatchLoop();
                return 0;
            }

            if (!ExecuteBuildCycle())
            {
                return 1;
            }

            s_totalTimer.Stop();
            PrintSummary();
            return 0;
        }

        private static bool ExecuteBuildCycle()
        {
            // Phase 2: Script Scanning
            PrintSection(2, 4, "DISCOVERY & SCRIPT SCANNING", ConsoleColor.Cyan);
            ScanProjectScripts();

            // Phase 3: Application Resources
            PrintSection(3, 4, "RESOURCE COMPILATION", ConsoleColor.Cyan);
            CompileResources();

            // Phase 4: Multi-Threaded Incremental Engine Compilation & Packaging
            PrintSection(4, 4, "PARALLEL INCREMENTAL ENGINE BUILD (DX12)", ConsoleColor.Cyan);
            bool didCompileOrLink = false;
            if (!BuildEditorParallel(out didCompileOrLink))
            {
                PrintError("Engine Editor build failed.");
                return false;
            }

            PackageEngine();

            // Standalone Game Runtime only if explicitly requested
            if (s_buildGame)
            {
                PrintSection(5, 5, "BUILDING GAME RUNTIME", ConsoleColor.Cyan);
                if (!BuildGameRuntime())
                {
                    PrintWarning("Game runtime build encountered warnings or errors.");
                }
            }

            return true;
        }

        private static void RunWatchLoop()
        {
            Console.WriteLine();
            PrintSection(0, 0, "CONTINUOUS WATCH MODE ACTIVE", ConsoleColor.Magenta);
            PrintInfo(string.Format("Monitoring all source changes across Engine, Editor, Plugins, and Projects"));
            PrintInfo(string.Format("Target build folder: {0}", s_buildRoot));
            PrintInfo("Whenever any file is changed, the engine will automatically recompile incrementally.");
            PrintInfo("Press Ctrl+C to terminate watch mode.\n");

            // Initial build cycle
            ExecuteBuildCycle();

            object syncLock = new object();
            bool rebuildPending = false;
            Timer debounceTimer = null;
            debounceTimer = new Timer(state =>
            {
                lock (syncLock)
                {
                    if (rebuildPending)
                    {
                        rebuildPending = false;
                        Console.WriteLine();
                        PrintInfo(string.Format("[{0:HH:mm:ss}] Change detected! Rebuilding changed files...", DateTime.Now));
                        Stopwatch sw = Stopwatch.StartNew();
                        bool ok = ExecuteBuildCycle();
                        sw.Stop();
                        if (ok)
                        {
                            PrintSuccess(string.Format("[{0:HH:mm:ss}] Engine successfully updated in {1:F2}s. Waiting for next change...",
                                DateTime.Now, sw.Elapsed.TotalSeconds));
                        }
                        else
                        {
                            PrintWarning(string.Format("[{0:HH:mm:ss}] Build had errors. Fix and save to recompile...", DateTime.Now));
                        }
                    }
                }
            }, null, Timeout.Infinite, Timeout.Infinite);

            List<FileSystemWatcher> watchers = new List<FileSystemWatcher>();
            List<string> watchDirs = new List<string>
            {
                Path.Combine(s_projectDir, "Engine"),
                Path.Combine(s_projectDir, "Editor"),
                Path.Combine(s_projectDir, "Plugins")
            };
            if (!string.IsNullOrEmpty(s_projectsRoot) && Directory.Exists(s_projectsRoot))
            {
                watchDirs.Add(s_projectsRoot);
            }

            Action<string> onFileTrigger = (filePath) =>
            {
                string ext = Path.GetExtension(filePath).ToLowerInvariant();
                if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".rc" || ext == ".hlsl" || ext == ".glsl")
                {
                    lock (syncLock)
                    {
                        rebuildPending = true;
                        try { debounceTimer.Change(300, Timeout.Infinite); } catch { }
                    }
                }
            };

            foreach (var dir in watchDirs)
            {
                if (Directory.Exists(dir))
                {
                    try
                    {
                        FileSystemWatcher w = new FileSystemWatcher(dir);
                        w.IncludeSubdirectories = true;
                        w.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size;
                        w.Changed += (s, e) => onFileTrigger(e.FullPath);
                        w.Created += (s, e) => onFileTrigger(e.FullPath);
                        w.Deleted += (s, e) => onFileTrigger(e.FullPath);
                        w.Renamed += (s, e) => { onFileTrigger(e.OldFullPath); onFileTrigger(e.FullPath); };
                        w.EnableRaisingEvents = true;
                        watchers.Add(w);
                    }
                    catch (Exception ex)
                    {
                        PrintWarning(string.Format("Failed to watch directory {0}: {1}", dir, ex.Message));
                    }
                }
            }

            using (AutoResetEvent waitHandle = new AutoResetEvent(false))
            {
                Console.CancelKeyPress += (s, e) =>
                {
                    e.Cancel = true;
                    waitHandle.Set();
                };
                waitHandle.WaitOne();
            }

            foreach (var w in watchers)
            {
                try { w.Dispose(); } catch { }
            }
            try { debounceTimer.Dispose(); } catch { }
            PrintInfo("Watch mode terminated.");
        }

        private static void ParseArguments(string[] args)
        {
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i].Trim();
                if (arg.Equals("--game", StringComparison.OrdinalIgnoreCase) || arg.Equals("-g", StringComparison.OrdinalIgnoreCase) ||
                    arg.Equals("--game-only", StringComparison.OrdinalIgnoreCase))
                {
                    s_buildGame = true;
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
                    s_buildGame = true;
                    if (i + 1 < args.Length)
                    {
                        s_customProjectPath = args[++i];
                    }
                }
                else if (arg.Equals("-j", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                {
                    int threads;
                    if (int.TryParse(args[++i], out threads) && threads > 0)
                    {
                        s_maxConcurrency = threads;
                    }
                }
                else if ((arg.Equals("--build-dir", StringComparison.OrdinalIgnoreCase) || arg.Equals("-b", StringComparison.OrdinalIgnoreCase)) && i + 1 < args.Length)
                {
                    s_customBuildRoot = args[++i];
                }
                else if (arg.Equals("--watch", StringComparison.OrdinalIgnoreCase) || arg.Equals("-w", StringComparison.OrdinalIgnoreCase))
                {
                    s_watch = true;
                }
                else if (arg.Equals("--help", StringComparison.OrdinalIgnoreCase) || arg.Equals("-h", StringComparison.OrdinalIgnoreCase) || arg.Equals("/?"))
                {
                    PrintHelp();
                    Environment.Exit(0);
                }
                else if (!arg.StartsWith("-") && Directory.Exists(arg))
                {
                    s_customProjectPath = arg;
                    s_buildGame = true;
                }
            }
        }

        private static void PrintHelp()
        {
            Console.WriteLine(@"
Eunoia Engine Multi-Threaded Build System (.NET)
Usage: Build.bat [options] [ProjectPath]

Options:
  -w, --watch             Watch mode: automatically incremental rebuild on any file changes
  -g, --game              Build the Standalone Game Runtime in addition to the engine
  -p, --project <path>    Specify game project directory (builds game runtime for project)
  -b, --build-dir <path>  Specify custom build output directory
  -j <threads>            Set maximum parallel compilation worker threads (default: CPU cores)
  -c, --clean             Clean all object files, build intermediates, and outputs
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
                  MULTI-THREADED INCREMENTAL BUILD SYSTEM (.NET)
");
            Console.ResetColor();
            PrintInfo(string.Format("Engine Root:    {0}", s_projectDir));
            PrintInfo(string.Format("Build Output:   {0}", s_outputDir));
            PrintInfo(string.Format("Parallel Cores: {0} CPU Threads", s_maxConcurrency));
            Console.WriteLine();
        }

        private static void PromptBuildModeSelection()
        {
            if (Console.IsInputRedirected) return;

            string[] options = new string[]
            {
                "Fast Build  (Incremental - compile modified files & changes only)",
                "Clean Build (Rebuild from scratch - delete intermediates and recompile all)"
            };

            int selectedIndex = 0;
            ConsoleKey key;

            Console.CursorVisible = false;
            try
            {
                while (true)
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("Use Up/Down Arrow keys to select build mode, then press Enter:");
                    Console.ResetColor();

                    for (int i = 0; i < options.Length; i++)
                    {
                        if (i == selectedIndex)
                        {
                            Console.ForegroundColor = ConsoleColor.Black;
                            Console.BackgroundColor = ConsoleColor.Cyan;
                            Console.WriteLine(string.Format(" > [x] {0} ", options[i]));
                            Console.ResetColor();
                        }
                        else
                        {
                            Console.ForegroundColor = ConsoleColor.DarkGray;
                            Console.WriteLine(string.Format("   [ ] {0} ", options[i]));
                            Console.ResetColor();
                        }
                    }

                    key = Console.ReadKey(true).Key;

                    if (key == ConsoleKey.UpArrow)
                    {
                        selectedIndex--;
                        if (selectedIndex < 0) selectedIndex = options.Length - 1;
                    }
                    else if (key == ConsoleKey.DownArrow)
                    {
                        selectedIndex++;
                        if (selectedIndex >= options.Length) selectedIndex = 0;
                    }
                    else if (key == ConsoleKey.Enter)
                    {
                        break;
                    }

                    // Reset cursor up to redraw menu
                    int linesToMove = options.Length + 1;
                    try
                    {
                        Console.SetCursorPosition(0, Math.Max(0, Console.CursorTop - linesToMove));
                    }
                    catch
                    {
                        // In case console buffer doesn't support setting cursor
                        Console.WriteLine();
                    }
                }
            }
            finally
            {
                Console.CursorVisible = true;
            }

            Console.WriteLine();
            if (selectedIndex == 1)
            {
                PrintInfo("Selected: Clean Build (Rebuild all components from scratch)");
                s_clean = true;
            }
            else
            {
                PrintInfo("Selected: Fast Build (Incremental - changes only)");
                s_clean = false;
            }
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

        private static void CleanIntermediates()
        {
            try
            {
                if (Directory.Exists(s_intermediatesDir))
                {
                    PrintInfo(string.Format("Cleaning intermediate build artifacts: {0}", s_intermediatesDir));
                    Directory.Delete(s_intermediatesDir, true);
                }
                string editorExe = Path.Combine(s_outputDir, "Eunoia-Editor.exe");
                if (File.Exists(editorExe))
                {
                    File.Delete(editorExe);
                }
                string gameExe = Path.Combine(s_outputDir, "Eunoia-Game.exe");
                if (File.Exists(gameExe))
                {
                    File.Delete(gameExe);
                }
            }
            catch (Exception ex)
            {
                PrintWarning(string.Format("Could not delete some intermediate files: {0}", ex.Message));
            }
        }

        private static void EnsureDirectories()
        {
            string[] dirs = new string[]
            {
                s_buildRoot,
                s_outputDir,
                s_intermediatesDir,
                s_objDir,
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
            // Compiler (g++ / gcc)
            string localGpp = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "g++.exe");
            string localGcc = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "gcc.exe");
            if (File.Exists(localGpp))
            {
                s_compilerExe = localGpp;
                s_gccExe = File.Exists(localGcc) ? localGcc : localGpp;
            }
            else
            {
                s_compilerExe = FindInPath("g++.exe");
                s_gccExe = FindInPath("gcc.exe") ?? s_compilerExe;
            }

            if (string.IsNullOrEmpty(s_compilerExe))
            {
                PrintError("C++ Compiler (g++) not found! Ensure w64devkit or g++ is available.");
                return false;
            }

            PrintSuccess(string.Format("Compiler:       {0}", s_compilerExe));

            // CCache (Compiler Cache Acceleration)
            string localCcache = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "ccache.exe");
            if (File.Exists(localCcache))
            {
                s_ccacheExe = localCcache;
                PrintSuccess(string.Format("CCache:         {0} (Acceleration Active)", s_ccacheExe));
            }
            else
            {
                s_ccacheExe = FindInPath("ccache.exe");
                if (!string.IsNullOrEmpty(s_ccacheExe))
                {
                    PrintSuccess(string.Format("CCache:         {0} (Acceleration Active)", s_ccacheExe));
                }
            }

            // Windres (Resource compiler)
            string localWindres = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "windres.exe");
            s_windresExe = File.Exists(localWindres) ? localWindres : FindInPath("windres.exe");
            if (!string.IsNullOrEmpty(s_windresExe))
            {
                PrintSuccess(string.Format("Resource Comp:  {0}", s_windresExe));
            }

            // CMake & Ninja
            string localCmake = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "cmake.exe");
            s_cmakeExe = File.Exists(localCmake) ? localCmake : FindInPath("cmake.exe");
            string localNinja = Path.Combine(s_projectDir, "tools", "w64devkit", "bin", "ninja.exe");
            s_ninjaExe = File.Exists(localNinja) ? localNinja : FindInPath("ninja.exe");

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
            if (File.Exists(outObj) && File.GetLastWriteTimeUtc(outObj) >= File.GetLastWriteTimeUtc(rcFile))
            {
                s_resObj = outObj;
                PrintSuccess(string.Format("Resource object up to date: {0}", Path.GetFileName(outObj)));
                return;
            }

            string args = string.Format("-I\"{0}\" -i \"{1}\" -O coff -o \"{2}\"", s_projectDir, rcFile, outObj);
            bool success = RunProcessDirect(s_windresExe, args);
            if (success && File.Exists(outObj))
            {
                s_resObj = outObj;
                PrintSuccess(string.Format("Resource object compiled: {0}", Path.GetFileName(outObj)));
            }
            else
            {
                PrintWarning("Resource compilation produced warnings. Proceeding without resource object.");
            }
        }

        private static bool IsUnitUpToDate(CompileUnit unit)
        {
            if (!File.Exists(unit.ObjectPath)) return false;
            DateTime objTime = File.GetLastWriteTimeUtc(unit.ObjectPath);
            DateTime srcTime = File.GetLastWriteTimeUtc(unit.SourcePath);
            if (srcTime > objTime) return false;

            string depPath = Path.ChangeExtension(unit.ObjectPath, ".d");
            if (File.Exists(depPath))
            {
                try
                {
                    string content = File.ReadAllText(depPath);
                    int colon = content.IndexOf(':');
                    if (colon >= 0) content = content.Substring(colon + 1);
                    content = content.Replace("\\\r\n", " ").Replace("\\\n", " ");
                    string[] parts = content.Split(new char[] { ' ', '\r', '\n', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                    foreach (var part in parts)
                    {
                        string headerPath = part.Trim().Trim('"');
                        if (string.IsNullOrEmpty(headerPath) || headerPath == "\\") continue;
                        if (File.Exists(headerPath))
                        {
                            if (File.GetLastWriteTimeUtc(headerPath) > objTime)
                            {
                                return false;
                            }
                        }
                    }
                    return true;
                }
                catch { }
            }

            string headerSameName = Path.ChangeExtension(unit.SourcePath, ".h");
            if (File.Exists(headerSameName) && File.GetLastWriteTimeUtc(headerSameName) > objTime)
            {
                return false;
            }

            if (Path.GetFileName(unit.SourcePath).Equals("Main.cpp", StringComparison.OrdinalIgnoreCase))
            {
                string uiHeader = Path.Combine(s_projectDir, "Editor", "Editor", "Include", "Editor", "EngineUI.h");
                if (File.Exists(uiHeader) && File.GetLastWriteTimeUtc(uiHeader) > objTime)
                {
                    return false;
                }
            }

            return true;
        }

        private static bool BuildEditorParallel(out bool didCompileOrLink)
        {
            didCompileOrLink = false;
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
                Path.Combine(s_projectDir, "Plugins", "Post-Process-System", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Water", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Include"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Collisions", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Physics", "Include"),
                Path.Combine(s_projectDir, "Plugins", "RayTrace", "Include"),
                Path.Combine(s_projectDir, "Plugins", "SaveSystem", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Texture-Streaming", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty"),
                Path.Combine(s_projectDir, "Editor", "Editor", "Include"),
                Path.Combine(s_projectDir, "deps", "tinyobj"),
                Path.Combine(s_projectDir, "deps", "cgltf"),
                Path.Combine(s_projectDir, "deps", "ufbx"),
                Path.Combine(s_projectDir, "deps", "meshoptimizer"),
                Path.Combine(s_projectDir, "deps", "imgui"),
                Path.Combine(s_projectDir, "deps", "imgui", "backends"),
                Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "include"),
                Path.Combine(s_projectDir, "deps", "glm"),
                Path.Combine(s_projectDir, "deps", "json"),
                s_projectDir
            };

            List<string> sourceFiles = new List<string>
            {
                Path.Combine(s_projectDir, "deps", "meshoptimizer", "meshoptimizer_unity.cpp"),
                Path.Combine(s_projectDir, "deps", "ufbx", "ufbx.c"),
                Path.Combine(s_projectDir, "Plugins", "Post-Process-System", "Source", "PostProcessSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Post-Process-System", "Source", "PostProcessPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Water", "Source", "WaterSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Water", "Source", "WaterPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Source", "GraphicsSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Source", "GraphicsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Texture-Streaming", "Source", "TextureStreamingSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Texture-Streaming", "Source", "TextureStreamingPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "UISystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "UIPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "CustomUIRenderer.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "CustomUIWidget.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "CustomUIManager.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Collisions", "Source", "CollisionSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Collisions", "Source", "CollisionsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Physics", "Source", "PhysicsSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Physics", "Source", "PhysicsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "RayTrace", "Source", "RayTraceSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "RayTrace", "Source", "RayTracePlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "SaveSystem", "Source", "SaveSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "SaveSystem", "Source", "SaveSystemPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty", "imgui_node_editor.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty", "imgui_node_editor_api.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty", "imgui_canvas.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty", "crude_json.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty", "drawing.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "ThirdParty", "widgets.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "Source", "NodeBehaviourGraph.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "Source", "NodeBehaviourInstance.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "Source", "NodeBehaviourEditor.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "Source", "NodeBehaviourManager.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Node-Behaviour", "Source", "NodeBehaviourPlugin.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "MeshImporter.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "TextureManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "DirectXTex.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "DirectXTexD3D12.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetRegistry.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Src", "EunoiaBehaviour.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Src", "SceneCacheManager.cpp"),
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

            sourceFiles.AddRange(s_projectScripts);

            // Generate include string
            StringBuilder incSb = new StringBuilder();
            foreach (var inc in includes)
            {
                incSb.Append(string.Format("-I\"{0}\" ", inc));
            }
            string incFlags = incSb.ToString();

            // Prepare list of compilation units
            List<CompileUnit> units = new List<CompileUnit>();
            foreach (var src in sourceFiles)
            {
                if (!File.Exists(src)) continue;
                string rel = GetRelativeOrHashedPath(src, s_projectDir);
                string objName = rel.Replace('\\', '_').Replace('/', '_').Replace(':', '_') + ".o";
                string objPath = Path.Combine(s_objDir, objName);

                bool isC = src.EndsWith(".c", StringComparison.OrdinalIgnoreCase);
                units.Add(new CompileUnit
                {
                    SourcePath = src,
                    ObjectPath = objPath,
                    IsC = isC
                });
            }

            // Identify units that need compilation
            int upToDateCount = 0;
            List<CompileUnit> toCompile = new List<CompileUnit>();
            foreach (var unit in units)
            {
                if (IsUnitUpToDate(unit))
                {
                    upToDateCount++;
                }
                else
                {
                    toCompile.Add(unit);
                }
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.White;
            Console.Write("  [STAGE] ");
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(string.Format("Compiling {0} translation units in parallel ({1} up to date, {2} to build on {3} threads)...",
                units.Count, upToDateCount, toCompile.Count, s_maxConcurrency));
            Console.ResetColor();

            Stopwatch parallelTimer = Stopwatch.StartNew();
            ConcurrentBag<string> errors = new ConcurrentBag<string>();
            ConcurrentBag<string> warnings = new ConcurrentBag<string>();

            int completed = upToDateCount;
            int total = units.Count;
            int activeWorkers = 0;

            bool compileFailed = false;

            if (toCompile.Count > 0)
            {
                using (CancellationTokenSource cts = new CancellationTokenSource())
                {
                    // Dynamic live progress bar thread
                    bool progressDone = false;
                    char[] spinner = new char[] { '|', '/', '-', '\\' };
                    int spinIdx = 0;

                    Thread progressThread = new Thread(() =>
                    {
                        while (!progressDone)
                        {
                            int cur = Volatile.Read(ref completed);
                            int act = Volatile.Read(ref activeWorkers);
                            double el = parallelTimer.Elapsed.TotalSeconds;
                            int percent = total > 0 ? (cur * 100) / total : 100;
                            string bar = GenerateProgressBar(percent, 24);

                            try
                            {
                                Console.CursorLeft = 0;
                                Console.ForegroundColor = ConsoleColor.DarkGray;
                                Console.Write("    [");
                                Console.ForegroundColor = ConsoleColor.Green;
                                Console.Write(spinner[spinIdx % spinner.Length]);
                                Console.ForegroundColor = ConsoleColor.DarkGray;
                                Console.Write("] ");
                                Console.ForegroundColor = ConsoleColor.Yellow;
                                Console.Write(bar);
                                Console.ForegroundColor = ConsoleColor.White;
                                Console.Write(string.Format(" {0}/{1} ({2} active) {3:F1}s   ", cur, total, act, el));
                                Console.ResetColor();
                            }
                            catch { }

                            spinIdx++;
                            Thread.Sleep(80);
                        }
                    });

                    progressThread.Start();

                    // Parallel compile loop
                    ParallelOptions pOpts = new ParallelOptions
                    {
                        MaxDegreeOfParallelism = s_maxConcurrency,
                        CancellationToken = cts.Token
                    };

                    try
                    {
                        Parallel.ForEach(toCompile, pOpts, (unit, loopState) =>
                        {
                            if (compileFailed)
                            {
                                loopState.Stop();
                                return;
                            }

                            Interlocked.Increment(ref activeWorkers);

                            string compiler = unit.IsC ? s_gccExe : s_compilerExe;
                            string langFlag = unit.IsC ? "-O2 -pipe" : "-std=c++20 -O2 -pipe -fno-strict-aliasing -fext-numeric-literals";
                            string depPath = Path.ChangeExtension(unit.ObjectPath, ".d");
                            string args = string.Format("{0} {1} -MMD -MF \"{2}\" -c \"{3}\" -o \"{4}\"",
                                langFlag, incFlags, depPath, unit.SourcePath, unit.ObjectPath);

                            string execPath = compiler;
                            if (!string.IsNullOrEmpty(s_ccacheExe))
                            {
                                execPath = s_ccacheExe;
                                args = string.Format("\"{0}\" {1}", compiler, args);
                            }

                            string unitErr = "";
                            string unitWarn = "";
                            bool success = RunProcessCapture(execPath, args, s_projectDir, out unitErr, out unitWarn);

                            if (!string.IsNullOrWhiteSpace(unitWarn))
                            {
                                warnings.Add(string.Format("{0}: {1}", Path.GetFileName(unit.SourcePath), unitWarn));
                            }

                            if (!success || !File.Exists(unit.ObjectPath))
                            {
                                compileFailed = true;
                                errors.Add(string.Format("{0}: {1}", Path.GetFileName(unit.SourcePath), unitErr));
                                loopState.Stop();
                            }

                            Interlocked.Decrement(ref activeWorkers);
                            Interlocked.Increment(ref completed);
                        });
                    }
                    catch (Exception ex)
                    {
                        if (!compileFailed) errors.Add(ex.Message);
                    }

                    progressDone = true;
                    progressThread.Join();
                }
            }

            parallelTimer.Stop();

            // Clear progress line
            try
            {
                Console.CursorLeft = 0;
                string fullBar = GenerateProgressBar(compileFailed ? (completed * 100) / total : 100, 24);
                Console.ForegroundColor = ConsoleColor.DarkGray;
                Console.Write("    [");
                Console.ForegroundColor = compileFailed ? ConsoleColor.Red : ConsoleColor.Green;
                Console.Write(compileFailed ? "XX" : "OK");
                Console.ForegroundColor = ConsoleColor.DarkGray;
                Console.Write("] ");
                Console.ForegroundColor = compileFailed ? ConsoleColor.Red : ConsoleColor.Green;
                Console.Write(fullBar);
                Console.ForegroundColor = ConsoleColor.White;
                Console.WriteLine(string.Format(" {0}/{1} objects compiled in {2:F2}s  ", completed, total, parallelTimer.Elapsed.TotalSeconds));
                Console.ResetColor();
            }
            catch { }

            if (warnings.Count > 0 && !s_verbose)
            {
                PrintWarning(string.Format("{0} compilation warnings encountered.", warnings.Count));
            }

            if (errors.Count > 0 || compileFailed)
            {
                PrintError(string.Format("{0} compilation error(s) encountered:", errors.Count));
                foreach (var err in errors)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine(string.Format("      [ERROR] {0}", err));
                    Console.ResetColor();
                }
                return false;
            }

            // Link step
            bool needLink = toCompile.Count > 0 || !File.Exists(editorExe);
            if (!needLink)
            {
                DateTime exeTime = File.GetLastWriteTimeUtc(editorExe);
                foreach (var unit in units)
                {
                    if (File.Exists(unit.ObjectPath) && File.GetLastWriteTimeUtc(unit.ObjectPath) > exeTime)
                    {
                        needLink = true;
                        break;
                    }
                }
                if (!string.IsNullOrEmpty(s_resObj) && File.Exists(s_resObj) && File.GetLastWriteTimeUtc(s_resObj) > exeTime)
                {
                    needLink = true;
                }
            }

            if (!needLink)
            {
                PrintSection(0, 0, "LINKING EUNOIA-EDITOR", ConsoleColor.Green);
                FileInfo fi = new FileInfo(editorExe);
                PrintSuccess(string.Format("Eunoia-Editor.exe is up to date ({0:F2} MB)", fi.Length / (1024.0 * 1024.0)));
                return true;
            }

            didCompileOrLink = true;
            PrintSection(0, 0, "LINKING EUNOIA-EDITOR", ConsoleColor.Green);
            Stopwatch linkTimer = Stopwatch.StartNew();

            StringBuilder linkArgs = new StringBuilder();
            foreach (var unit in units)
            {
                linkArgs.Append(string.Format("\"{0}\" ", unit.ObjectPath));
            }

            if (!string.IsNullOrEmpty(s_resObj) && File.Exists(s_resObj))
            {
                linkArgs.Append(string.Format("\"{0}\" ", s_resObj));
            }

            string libDir = Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64");
            linkArgs.Append(string.Format("-L\"{0}\" ", libDir));
            linkArgs.Append("-lglfw3 -ld3d12 -ldxgi -ld3dcompiler -lgdi32 -limm32 -lcomdlg32 -lshell32 -lole32 -lopengl32 -ldwmapi ");
            linkArgs.Append(string.Format("-o \"{0}\"", editorExe));

            bool linkSuccess = RunProcessDirect(s_compilerExe, linkArgs.ToString());
            linkTimer.Stop();

            if (linkSuccess && File.Exists(editorExe))
            {
                FileInfo fi = new FileInfo(editorExe);
                PrintSuccess(string.Format("Eunoia-Editor.exe linked successfully in {0:F2}s! ({1:F2} MB)",
                    linkTimer.Elapsed.TotalSeconds, fi.Length / (1024.0 * 1024.0)));
                return true;
            }
            else
            {
                PrintError("Linking failed.");
                return false;
            }
        }

        private static string GetRelativeOrHashedPath(string fullPath, string basePath)
        {
            try
            {
                if (fullPath.StartsWith(basePath, StringComparison.OrdinalIgnoreCase))
                {
                    return fullPath.Substring(basePath.Length).TrimStart('\\', '/');
                }
                return Path.GetFileName(fullPath) + "_" + fullPath.GetHashCode().ToString("X8");
            }
            catch
            {
                return Path.GetFileName(fullPath);
            }
        }

        private static void PackageEngine()
        {
            PrintInfo("Packaging engine binaries, libraries, shaders, and resources (WickedEngine layout)...");

            // Copy DLLs
            CopyFileIfExists(Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64", "glfw3.dll"), Path.Combine(s_outputDir, "glfw3.dll"));

            // Sync runtime directories (Resources & Shaders)
            SyncDirectory(Path.Combine(s_projectDir, "Resources"), Path.Combine(s_outputDir, "Resources"));
            SyncDirectory(Path.Combine(s_projectDir, "Shaders"), Path.Combine(s_outputDir, "Shaders"));

            // Create Run.bat launcher
            string runBat = Path.Combine(s_outputDir, "Run.bat");
            if (!File.Exists(runBat))
            {
                File.WriteAllText(runBat, "@echo off\r\ncd /d \"%~dp0\"\r\necho [INFO] Starting Eunoia-Editor [DirectX 12]...\r\nstart \"\" \"Eunoia-Editor.exe\"\r\n");
            }

            PrintSuccess("Engine distribution packaged into: " + s_outputDir);
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
                Path.Combine(s_projectDir, "Plugins", "Post-Process-System", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Graphics", "Include"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Collisions", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Physics", "Include"),
                Path.Combine(s_projectDir, "Plugins", "RayTrace", "Include"),
                Path.Combine(s_projectDir, "Plugins", "SaveSystem", "Include"),
                Path.Combine(s_projectDir, "Plugins", "Texture-Streaming", "Include"),
                Path.Combine(s_projectDir, "deps", "tinyobj"),
                Path.Combine(s_projectDir, "deps", "cgltf"),
                Path.Combine(s_projectDir, "deps", "ufbx"),
                Path.Combine(s_projectDir, "deps", "meshoptimizer"),
                Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "include"),
                Path.Combine(s_projectDir, "deps", "glm"),
                Path.Combine(s_projectDir, "deps", "json"),
                s_projectDir
            };

            List<string> sources = new List<string>
            {
                Path.Combine(s_projectDir, "deps", "meshoptimizer", "meshoptimizer_unity.cpp"),
                Path.Combine(s_projectDir, "deps", "ufbx", "ufbx.c"),
                Path.Combine(s_projectDir, "Plugins", "Post-Process-System", "Source", "PostProcessSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Post-Process-System", "Source", "PostProcessPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "UISystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "UISystem", "Source", "UIPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Collisions", "Source", "CollisionSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Collisions", "Source", "CollisionsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Physics", "Source", "PhysicsSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "Physics", "Source", "PhysicsPlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "RayTrace", "Source", "RayTraceSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "RayTrace", "Source", "RayTracePlugin.cpp"),
                Path.Combine(s_projectDir, "Plugins", "SaveSystem", "Source", "SaveSystem.cpp"),
                Path.Combine(s_projectDir, "Plugins", "SaveSystem", "Source", "SaveSystemPlugin.cpp"),
                Path.Combine(s_projectDir, "Engine", "EnginePlatform", "Src", "Window.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineScene", "Src", "EunoiaBehaviour.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetRegistry.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "AssetManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "TextureManager.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "DirectXTex.cpp"),
                Path.Combine(s_projectDir, "Engine", "EngineAssets", "Src", "MeshImporter.cpp"),
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
            sb.Append("-std=c++20 -O2 -pipe -fno-strict-aliasing -fext-numeric-literals ");
            foreach (var inc in includes) sb.Append(string.Format("-I\"{0}\" ", inc));
            foreach (var src in sources) sb.Append(string.Format("\"{0}\" ", src));

            string libDir = Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64");
            sb.Append(string.Format("-L\"{0}\" ", libDir));
            sb.Append("-lglfw3 -lgdi32 -lole32 -lshell32 ");

            if (!string.IsNullOrEmpty(s_resObj)) sb.Append(string.Format("\"{0}\" ", s_resObj));

            sb.Append(string.Format("-o \"{0}\"", gameExe));

            Stopwatch sw = Stopwatch.StartNew();
            bool success = RunProcessDirect(s_compilerExe, sb.ToString());
            sw.Stop();

            if (success && File.Exists(gameExe))
            {
                string projExe = Path.Combine(gameBuildDir, string.Format("{0}.exe", projName));
                try { File.Copy(gameExe, projExe, true); } catch { }

                CopyFileIfExists(Path.Combine(s_projectDir, "deps", "glfw-3.5.1.bin.WIN64", "lib-mingw-w64", "glfw3.dll"), Path.Combine(gameBuildDir, "glfw3.dll"));

                SyncDirectory(Path.Combine(s_projectDir, "Shaders"), Path.Combine(gameBuildDir, "Shaders"));
                SyncDirectory(Path.Combine(s_projectDir, "Resources"), Path.Combine(gameBuildDir, "Resources"));

                if (Directory.Exists(Path.Combine(targetProj, "Cooked"))) SyncDirectory(Path.Combine(targetProj, "Cooked"), Path.Combine(gameBuildDir, "Cooked"));
                if (Directory.Exists(Path.Combine(targetProj, "Content"))) SyncDirectory(Path.Combine(targetProj, "Content"), Path.Combine(gameBuildDir, "Content"));
                if (Directory.Exists(Path.Combine(targetProj, "Scenes"))) SyncDirectory(Path.Combine(targetProj, "Scenes"), Path.Combine(gameBuildDir, "Scenes"));
                if (Directory.Exists(Path.Combine(targetProj, "Materials"))) SyncDirectory(Path.Combine(targetProj, "Materials"), Path.Combine(gameBuildDir, "Materials"));
                if (Directory.Exists(Path.Combine(targetProj, "Models"))) SyncDirectory(Path.Combine(targetProj, "Models"), Path.Combine(gameBuildDir, "Models"));

                string gameRunBat = Path.Combine(gameBuildDir, "Run.bat");
                File.WriteAllText(gameRunBat, string.Format("@echo off\r\ncd /d \"%~dp0\"\r\necho [INFO] Starting {0}...\r\nstart \"\" \"test.exe\"\r\n", projName));

                PrintSuccess(string.Format("Game runtime packaged in {0:F2}s into: {1}", sw.Elapsed.TotalSeconds, gameBuildDir));
                return true;
            }
            return false;
        }

        private static bool RunProcessDirect(string exePath, string args)
        {
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

            using (Process p = Process.Start(psi))
            {
                string err = p.StandardError.ReadToEnd();
                string outStr = p.StandardOutput.ReadToEnd();
                p.WaitForExit();
                if (p.ExitCode != 0)
                {
                    if (!string.IsNullOrWhiteSpace(err)) Console.Error.WriteLine(err);
                    if (!string.IsNullOrWhiteSpace(outStr)) Console.WriteLine(outStr);
                }
                return p.ExitCode == 0;
            }
        }

        private static bool RunProcessCapture(string exePath, string args, string workingDir, out string errOutput, out string warnOutput)
        {
            StringBuilder outSb = new StringBuilder();
            StringBuilder errSb = new StringBuilder();

            ProcessStartInfo psi = new ProcessStartInfo
            {
                FileName = exePath,
                Arguments = args,
                WorkingDirectory = workingDir,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            using (Process p = new Process { StartInfo = psi })
            {
                p.OutputDataReceived += (s, e) => { if (!string.IsNullOrEmpty(e.Data)) outSb.AppendLine(e.Data); };
                p.ErrorDataReceived += (s, e) => { if (!string.IsNullOrEmpty(e.Data)) errSb.AppendLine(e.Data); };

                p.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                p.WaitForExit();

                string combined = outSb.ToString() + "\n" + errSb.ToString();
                errOutput = p.ExitCode != 0 ? combined : "";
                warnOutput = combined.IndexOf("warning:", StringComparison.OrdinalIgnoreCase) >= 0 ? combined : "";
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
            if (step > 0 && total > 0)
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
            Console.WriteLine(string.Format("| Parallel Worker Threads      | {0} threads", s_maxConcurrency).PadRight(79) + "|");
            Console.WriteLine(string.Format("| Discovered Scripts           | {0} scripts", s_projectScripts.Count).PadRight(79) + "|");
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine(@"+------------------------------+-----------------------------------------------+");
            Console.ResetColor();
            Console.WriteLine();
        }

        private class CompileUnit
        {
            public string SourcePath;
            public string ObjectPath;
            public bool IsC;
        }
    }
}
