#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.env.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_BIN = "bin";
    constexpr StringLiteral OPTION_INCLUDE = "include";
    constexpr StringLiteral OPTION_DEBUG_BIN = "debug-bin";
    constexpr StringLiteral OPTION_TOOLS = "tools";
    constexpr StringLiteral OPTION_PYTHON = "python";

    constexpr CommandSwitch SWITCHES[] = {
        {OPTION_BIN,
         [] {
             return msg::format(
                 msgCmdEnvOptions, msg::path = "bin/", msg::env_var = format_environment_variable("PATH"));
         }},
        {OPTION_INCLUDE,
         [] {
             return msg::format(
                 msgCmdEnvOptions, msg::path = "include/", msg::env_var = format_environment_variable("INCLUDE"));
         }},
        {OPTION_DEBUG_BIN,
         [] {
             return msg::format(
                 msgCmdEnvOptions, msg::path = "debug/bin/", msg::env_var = format_environment_variable("PATH"));
         }},
        {OPTION_TOOLS,
         [] {
             return msg::format(
                 msgCmdEnvOptions, msg::path = "tools/*/", msg::env_var = format_environment_variable("PATH"));
         }},
        {OPTION_PYTHON,
         [] {
             return msg::format(
                 msgCmdEnvOptions, msg::path = "python/", msg::env_var = format_environment_variable("PYTHONPATH"));
         }},
    };

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandEnvMetadata{
        "env",
        msgHelpEnvCommand,
        {
            "vcpkg env --triplet x64-windows",
            msgCommandEnvExample2,
            "vcpkg env \"ninja --version\" --triplet x64-windows",
        },
        Undocumented,
        AutocompletePriority::Public,
        0,
        1,
        {SWITCHES},
        nullptr,
    };

    // This command should probably optionally take a port
    void command_env_and_exit(const VcpkgCmdArguments& args,
                              const VcpkgPaths& paths,
                              Triplet triplet,
                              Triplet /*host_triplet*/)
    {
        const auto& fs = paths.get_filesystem();

        const ParsedArguments options = args.parse_arguments(CommandEnvMetadata);

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        var_provider.load_generic_triplet_vars(triplet);

        const PreBuildInfo pre_build_info(
            paths, triplet, var_provider.get_generic_triplet_vars(triplet).value_or_exit(VCPKG_LINE_INFO));
        const Toolset& toolset = paths.get_toolset(pre_build_info);
        auto build_env_cmd = make_build_env_cmd(pre_build_info, toolset);

        std::unordered_map<std::string, std::string> extra_env = {};
        const bool add_bin = Util::Sets::contains(options.switches, OPTION_BIN);
        const bool add_include = Util::Sets::contains(options.switches, OPTION_INCLUDE);
        const bool add_debug_bin = Util::Sets::contains(options.switches, OPTION_DEBUG_BIN);
        const bool add_tools = Util::Sets::contains(options.switches, OPTION_TOOLS);
        const bool add_python = Util::Sets::contains(options.switches, OPTION_PYTHON);

        std::vector<std::string> path_vars;
        const auto current_triplet_path = paths.installed().triplet_dir(triplet);
        if (add_bin) path_vars.push_back((current_triplet_path / "bin").native());
        if (add_debug_bin) path_vars.push_back((current_triplet_path / "debug" / "bin").native());
        if (add_include) extra_env.emplace("INCLUDE", (current_triplet_path / "include").native());
        if (add_tools)
        {
            auto tools_dir = current_triplet_path / "tools";
            path_vars.push_back(tools_dir.native());
            for (auto&& tool_dir : fs.get_directories_non_recursive(tools_dir, VCPKG_LINE_INFO))
            {
                path_vars.push_back(tool_dir.native());
            }
        }
        if (add_python)
        {
            extra_env.emplace("PYTHONPATH", (current_triplet_path / "python").native());
        }

        if (path_vars.size() > 0) extra_env.emplace("PATH", Strings::join(";", path_vars));
        for (auto&& passthrough : pre_build_info.passthrough_env_vars)
        {
            if (auto e = get_environment_variable(passthrough))
            {
                extra_env.emplace(passthrough, std::move(e.value_or_exit(VCPKG_LINE_INFO)));
            }
        }

#if defined(_WIN32)
        ProcessLaunchSettings settings;
        auto& env = settings.environment.emplace(get_modified_clean_environment(extra_env));
        if (!build_env_cmd.empty())
        {
            env = cmd_execute_and_capture_environment(build_env_cmd, env);
        }

        auto cmd = Command{"cmd"}.string_arg("/d");
        if (!options.command_arguments.empty())
        {
            cmd.string_arg("/c").raw_arg(options.command_arguments[0]);
        }

        enter_interactive_subprocess();
        auto rc = cmd_execute(cmd, settings);
        exit_interactive_subprocess();
        Checks::exit_with_code(VCPKG_LINE_INFO, rc.value_or_exit(VCPKG_LINE_INFO));
#else  // ^^^ _WIN32 / !_WIN32 vvv
        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgEnvPlatformNotSupported);
#endif // ^^^ !_WIN32
    }
} // namespace vcpkg
