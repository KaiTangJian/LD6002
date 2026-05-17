#!/usr/bin/env python3
"""
Compatibility wrapper for xrobot_gen_main.

This generator keeps the normal "module directory name == header name" behavior,
but lets a module manifest override the instantiated C++ type via:

  module_type: Namespace::ModuleType
"""

from pathlib import Path
import argparse

from xrobot.GenerateMain import (
    _format_cpp_value,
    _format_module_instance_statement,
    _format_template_arg,
    _generate_constexpr_header,
    _get_constexpr_namespace,
    _load_config_file,
    _require_mapping,
    auto_discover_modules,
    extract_constructor_args,
    extract_modules_from_config,
    parse_manifest_from_header,
)

MODULES_DIR = Path("Modules")


def resolve_module_type(module_name: str) -> str:
    header_path = MODULES_DIR / module_name / f"{module_name}.hpp"
    if not header_path.exists():
        raise FileNotFoundError(f"[ERROR] Module header not found: {header_path}")

    manifest = parse_manifest_from_header(header_path) or {}
    module_type = manifest.get("module_type", module_name)
    if not isinstance(module_type, str) or not module_type.strip():
        raise ValueError(
            f"[ERROR] Invalid module_type for module '{module_name}' in {header_path}"
        )

    return module_type.strip()


def generate_xrobot_main_code(hw_var: str, modules: list[str], config: dict) -> str:
    if not isinstance(config, dict):
        raise TypeError("[ERROR] top-level config must be a mapping")

    global_settings = _require_mapping(config.get("global_settings", {}), "global_settings")
    sleep_ms = global_settings.get("monitor_sleep_ms", 1000)
    constexpr_namespace = _get_constexpr_namespace(config)

    headers = [
        '#include "app_framework.hpp"',
        '#include "libxr.hpp"',
        "",
        "// Module headers",
    ] + [f'#include "{mod}.hpp"' for mod in modules]
    if config.get("constexprs"):
        headers.append('#include "xrobot_constexpr.hpp"')

    body = [
        f"static void XRobotMain(LibXR::HardwareContainer &{hw_var}) {{",
        "  using namespace LibXR;",
        "  ApplicationManager appmgr;",
        "",
        "  // Auto-generated module instantiations",
    ]

    module_entries = config.get("modules", [])
    if not isinstance(module_entries, list):
        raise TypeError("[ERROR] 'modules' must be a list of module instances")

    auto_inst_index = {}

    for entry in module_entries:
        if not isinstance(entry, dict):
            raise TypeError("[ERROR] each item in 'modules' must be a mapping")

        mod = entry.get("name")
        if not isinstance(mod, str) or not mod.strip():
            raise ValueError("[ERROR] each module entry requires non-empty string 'name'")
        mod = mod.strip()

        inst_id = entry.get("id")
        if inst_id is not None:
            if not isinstance(inst_id, str) or not inst_id.strip():
                raise ValueError(f"[ERROR] module '{mod}' has invalid non-empty string 'id'")
            instance_name = inst_id.strip()
        else:
            idx = auto_inst_index.get(mod, 0)
            instance_name = f"{mod.lower()}{idx}" if idx > 0 else mod.lower()
            auto_inst_index[mod] = idx + 1

        if mod not in modules:
            print(f"[WARN] Module {mod} not included in the provided list.")
            continue

        args_dict = _require_mapping(
            entry.get("constructor_args", {}),
            f"constructor_args for module '{mod}'",
        )
        args_list = [
            _format_cpp_value(v, k, constexpr_namespace=constexpr_namespace)
            for k, v in args_dict.items()
        ]

        tmpl_dict = _require_mapping(
            entry.get("template_args", {}),
            f"template_args for module '{mod}'",
        )
        tmpl_params = [
            _format_template_arg(v, constexpr_namespace=constexpr_namespace)
            for _, v in tmpl_dict.items()
        ]

        body.append(
            _format_module_instance_statement(
                resolve_module_type(mod), tmpl_params, instance_name, hw_var, args_list
            )
        )

    body += [
        "",
        "  while (true) {",
        "    appmgr.MonitorAll();",
        f"    Thread::Sleep({sleep_ms});",
        "  }",
        "}",
    ]

    return "\n".join(headers + [""] + body)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="XRobot code generation tool with manifest module_type support"
    )
    parser.add_argument("-o", "--output", default="User/xrobot_main.hpp", help="Output C++ file path")
    parser.add_argument("-m", "--modules", nargs="+", default=[], help="List of modules to include")
    parser.add_argument("--hw", default="hw", help="Hardware container variable name")
    parser.add_argument("-c", "--config", help="Configuration YAML file path")

    args = parser.parse_args()

    config_data = {}
    config_path = Path(args.config) if args.config else Path("User/xrobot.yaml")

    try:
        if config_path.exists():
            print(f"[INFO] Using existing configuration file: {config_path}")
            config_data = _load_config_file(config_path)
        elif args.config:
            print(f"[WARN] Configuration file not found: {config_path}")

        if not args.modules:
            args.modules = extract_modules_from_config(config_data)
            if args.modules:
                print(f"[INFO] Using modules from configuration: {', '.join(args.modules)}")
                for mod in args.modules:
                    hpp = MODULES_DIR / mod / f"{mod}.hpp"
                    if not hpp.exists():
                        print(f"[WARN] Module '{mod}' declared in config but header not found: {hpp}")
            else:
                args.modules = auto_discover_modules()
                print(f"Discovered modules: {', '.join(args.modules) or 'None'}")

        if not config_path.exists():
            config_data = extract_constructor_args(args.modules, MODULES_DIR, config_path)

        output_code = generate_xrobot_main_code(args.hw, args.modules, config_data)
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(output_code, encoding="utf-8")
        print(f"[SUCCESS] Generated entry file: {args.output}")

        constexpr_code = _generate_constexpr_header(config_data)
        if constexpr_code is not None:
            constexpr_path = output_path.with_name("xrobot_constexpr.hpp")
            constexpr_path.write_text(constexpr_code, encoding="utf-8")
            print(f"[SUCCESS] Generated constexpr header: {constexpr_path}")
    except (TypeError, ValueError, FileNotFoundError) as exc:
        parser.exit(1, f"{exc}\n")


if __name__ == "__main__":
    main()
