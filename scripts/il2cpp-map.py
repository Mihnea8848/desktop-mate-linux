#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# il2cpp-map.py UnityPlayer.dll OUT: maps the game's renamed il2cpp exports to
# their real names. UnityPlayer looks the exports up by name in the fixed order
# of Unity's il2cpp API list, so the n-th name string it loads is the n-th
# entry of NAMES (Unity 6000.3). Writes "renamed real" lines.
import re
import struct
import sys

NAMES = """
il2cpp_init il2cpp_init_utf16 il2cpp_shutdown il2cpp_set_config_dir
il2cpp_set_data_dir il2cpp_set_temp_dir il2cpp_set_commandline_arguments
il2cpp_set_commandline_arguments_utf16 il2cpp_set_config_utf16
il2cpp_set_config il2cpp_set_memory_callbacks
il2cpp_memory_pool_set_region_size il2cpp_memory_pool_get_region_size
il2cpp_get_corlib il2cpp_add_internal_call il2cpp_resolve_icall
il2cpp_alloc il2cpp_free il2cpp_array_class_get il2cpp_array_length
il2cpp_array_get_byte_length il2cpp_array_new il2cpp_array_new_specific
il2cpp_array_new_full il2cpp_bounded_array_class_get
il2cpp_array_element_size il2cpp_assembly_get_image il2cpp_class_for_each
il2cpp_class_enum_basetype il2cpp_class_is_inited il2cpp_class_is_generic
il2cpp_class_is_inflated il2cpp_class_is_assignable_from
il2cpp_class_is_subclass_of il2cpp_class_has_parent
il2cpp_class_from_il2cpp_type il2cpp_class_from_name
il2cpp_class_from_system_type il2cpp_class_get_element_class
il2cpp_class_get_events il2cpp_class_get_fields
il2cpp_class_get_nested_types il2cpp_class_get_interfaces
il2cpp_class_get_properties il2cpp_class_get_property_from_name
il2cpp_class_get_field_from_name il2cpp_class_get_methods
il2cpp_class_get_method_from_name il2cpp_class_get_name
il2cpp_type_get_name_chunked il2cpp_class_get_namespace
il2cpp_class_get_parent il2cpp_class_get_declaring_type
il2cpp_class_instance_size il2cpp_class_num_fields
il2cpp_class_is_valuetype il2cpp_class_value_size il2cpp_class_is_blittable
il2cpp_class_get_flags il2cpp_class_is_abstract il2cpp_class_is_interface
il2cpp_class_array_element_size il2cpp_class_from_type
il2cpp_class_get_type il2cpp_class_get_type_token
il2cpp_class_has_attribute il2cpp_class_has_references il2cpp_class_is_enum
il2cpp_class_get_image il2cpp_class_get_assemblyname il2cpp_class_get_rank
il2cpp_class_get_data_size il2cpp_class_get_static_field_data
il2cpp_stats_dump_to_file il2cpp_stats_get_value il2cpp_domain_get
il2cpp_domain_assembly_open il2cpp_domain_get_assemblies
il2cpp_raise_exception il2cpp_exception_from_name_msg
il2cpp_get_exception_argument_null il2cpp_format_exception
il2cpp_format_stack_trace il2cpp_unhandled_exception
il2cpp_native_stack_trace il2cpp_field_get_flags
il2cpp_field_get_from_reflection il2cpp_field_get_name
il2cpp_field_get_parent il2cpp_field_get_object il2cpp_field_get_offset
il2cpp_field_get_type il2cpp_field_get_value il2cpp_field_get_value_object
il2cpp_field_has_attribute il2cpp_field_set_value
il2cpp_field_static_get_value il2cpp_field_static_set_value
il2cpp_field_set_value_object il2cpp_field_is_literal il2cpp_gc_collect
il2cpp_gc_collect_a_little il2cpp_gc_start_incremental_collection
il2cpp_gc_disable il2cpp_gc_enable il2cpp_gc_is_disabled il2cpp_gc_set_mode
il2cpp_gc_get_max_time_slice_ns il2cpp_gc_set_max_time_slice_ns
il2cpp_gc_is_incremental il2cpp_gc_get_used_size il2cpp_gc_get_heap_size
il2cpp_gc_wbarrier_set_field il2cpp_gc_has_strict_wbarriers
il2cpp_gc_set_external_allocation_tracker
il2cpp_gc_set_external_wbarrier_tracker il2cpp_gc_foreach_heap
il2cpp_stop_gc_world il2cpp_start_gc_world il2cpp_gc_alloc_fixed
il2cpp_gc_free_fixed il2cpp_gchandle_new il2cpp_gchandle_new_weakref
il2cpp_gchandle_get_target il2cpp_gchandle_free
il2cpp_gchandle_foreach_get_target il2cpp_object_header_size
il2cpp_array_object_header_size
il2cpp_offset_of_array_length_in_array_object_header
il2cpp_offset_of_array_bounds_in_array_object_header
il2cpp_allocation_granularity il2cpp_unity_liveness_allocate_struct
il2cpp_unity_liveness_calculation_from_root
il2cpp_unity_liveness_calculation_from_statics
il2cpp_unity_liveness_finalize il2cpp_unity_liveness_free_struct
il2cpp_method_get_return_type il2cpp_method_get_declaring_type
il2cpp_method_get_name il2cpp_method_get_from_reflection
il2cpp_method_get_object il2cpp_method_is_generic il2cpp_method_is_inflated
il2cpp_method_is_instance il2cpp_method_get_param_count
il2cpp_method_get_param il2cpp_method_get_class il2cpp_method_has_attribute
il2cpp_method_get_flags il2cpp_method_get_token
il2cpp_method_get_param_name il2cpp_property_get_flags
il2cpp_property_get_get_method il2cpp_property_get_set_method
il2cpp_property_get_name il2cpp_property_get_parent il2cpp_object_get_class
il2cpp_object_get_size il2cpp_object_get_virtual_method il2cpp_object_new
il2cpp_object_unbox il2cpp_value_box il2cpp_monitor_enter
il2cpp_monitor_try_enter il2cpp_monitor_exit il2cpp_monitor_pulse
il2cpp_monitor_pulse_all il2cpp_monitor_wait il2cpp_monitor_try_wait
il2cpp_runtime_invoke il2cpp_runtime_invoke_convert_args
il2cpp_runtime_class_init il2cpp_runtime_object_init
il2cpp_runtime_object_init_exception
il2cpp_runtime_unhandled_exception_policy_set il2cpp_string_length
il2cpp_string_chars il2cpp_string_new il2cpp_string_new_len
il2cpp_string_new_utf16 il2cpp_string_new_wrapper il2cpp_string_intern
il2cpp_string_is_interned il2cpp_thread_current il2cpp_thread_attach
il2cpp_thread_detach il2cpp_is_vm_thread
il2cpp_current_thread_walk_frame_stack il2cpp_thread_walk_frame_stack
il2cpp_current_thread_get_top_frame il2cpp_thread_get_top_frame
il2cpp_current_thread_get_frame_at il2cpp_thread_get_frame_at
il2cpp_current_thread_get_stack_depth il2cpp_thread_get_stack_depth
il2cpp_override_stack_backtrace il2cpp_type_get_object il2cpp_type_get_type
il2cpp_type_get_class_or_element_class il2cpp_type_get_name
il2cpp_type_is_byref il2cpp_type_get_attrs il2cpp_type_equals
il2cpp_type_get_assembly_qualified_name il2cpp_type_get_reflection_name
il2cpp_type_is_static il2cpp_type_is_pointer_type il2cpp_image_get_assembly
il2cpp_image_get_name il2cpp_image_get_filename
il2cpp_image_get_entry_point il2cpp_image_get_class_count
il2cpp_image_get_class il2cpp_capture_memory_snapshot
il2cpp_free_captured_memory_snapshot il2cpp_set_find_plugin_callback
il2cpp_register_log_callback il2cpp_debugger_set_agent_options
il2cpp_is_debugger_attached il2cpp_register_debugger_agent_transport
il2cpp_debug_foreach_method il2cpp_debug_get_method_info
il2cpp_unity_install_unitytls_interface il2cpp_custom_attrs_from_class
il2cpp_custom_attrs_from_method il2cpp_custom_attrs_from_field
il2cpp_custom_attrs_get_attr il2cpp_custom_attrs_has_attr
il2cpp_custom_attrs_construct il2cpp_custom_attrs_free
il2cpp_class_set_userdata il2cpp_class_get_userdata_offset
il2cpp_set_default_thread_affinity
il2cpp_unity_set_android_network_up_state_func
""".split()


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    out = []
    for i in range(count):
        off = pe + 24 + opt + 40 * i
        name = data[off:off + 8].rstrip(b"\0").decode()
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
        out.append((name, va, max(vsize, rsize), raw))
    return out


def main():
    data = open(sys.argv[1], "rb").read()
    secs = sections(data)

    def rva_to_off(rva):
        for _, va, size, raw in secs:
            if va <= rva < va + size:
                return raw + rva - va
        return None

    _, tva, tsize, traw = next(s for s in secs if s[0] == ".text")
    # each lookup is: lea rdx,[name]; (short insn); call lookup; mov [global],rax
    hits = {}  # call target -> [(offset, name)]
    for m in re.finditer(rb"\x48\x8d\x15", data[traw:traw + tsize]):
        at = traw + m.start()
        for p in range(at + 7, at + 12):
            if data[p] == 0xE8 and data[p + 5:p + 8] == b"\x48\x89\x05":
                break
        else:
            continue
        disp = struct.unpack_from("<i", data, at + 3)[0]
        off = rva_to_off(tva + at - traw + 7 + disp)
        if off is None:
            continue
        end = data.find(b"\0", off, off + 80)
        name = data[off:end]
        if end > off and re.fullmatch(rb"[A-Za-z_][A-Za-z0-9_]{5,63}", name):
            target = p + 5 + struct.unpack_from("<i", data, p + 1)[0]
            hits.setdefault(target, []).append(name.decode())
    best = max(hits.values(), key=len, default=[])
    if len(best) != len(NAMES):
        sys.exit(f"il2cpp-map: found {len(best)} API names, expected {len(NAMES)}")
    with open(sys.argv[2], "w") as f:
        for renamed, real in zip(best, NAMES):
            f.write(f"{renamed} {real}\n")


if __name__ == "__main__":
    main()
