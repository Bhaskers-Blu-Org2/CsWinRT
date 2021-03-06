#pragma once

#include <functional>
#include <set>

namespace cswinrt
{
    using namespace winmd::reader;

    static inline bool starts_with(std::string_view const& value, std::string_view const& match) noexcept
    {
        return 0 == value.compare(0, match.size(), match);
    }

    static const struct
    {
        char const* csharp;
        char const* dotnet;
    }
    type_mappings[] =
    {
        {"bool", "Boolean"},
        {"char", "Char"},
        {"sbyte", "SByte"},
        {"byte", "Byte"},
        {"short", "Int16"},
        {"ushort", "UInt16"},
        {"int", "Int32"},
        {"uint", "UInt32"},
        {"long", "Int64"},
        {"ulong", "UInt64"},
        {"float", "Float"},
        {"double", "Double"},
        {"string", "String"},
    };

    auto to_csharp_type(fundamental_type type)
    {
        return type_mappings[(int)type].csharp;
    }

    auto to_dotnet_type(fundamental_type type)
    {
        return type_mappings[(int)type].dotnet;
    }

    auto get_delegate_type_suffix(fundamental_type type)
    {
        if (type == fundamental_type::String)
        {
            return "String";
        }
        return type_mappings[(int)type].dotnet;
    }

    static std::string get_vmethod_name(writer& w, TypeDef const& type, MethodDef const& method)
    {
        uint32_t const vtable_base = type.MethodList().first.index();
        uint32_t const vtable_index = method.index() - vtable_base;
        return w.write_temp("%_%", method.Name(), vtable_index);
    }

    bool is_type_blittable(type_semantics const& semantics)
    {
        return call(semantics,
            [&](object_type)
            {
                return false;
            },
            [&](type_definition const& type)
            {
                switch (get_category(type))
                {
                    case category::enum_type:
                        return true;
                    case category::struct_type:
                        if (auto mapping = get_mapped_type(type.TypeNamespace(), type.TypeName()))
                        {
                            return !mapping->requires_marshaling;
                        }

                        for (auto&& field : type.FieldList())
                        {
                            if (!is_type_blittable(get_type_semantics(field.Signature().Type())))
                            {
                                return false;
                            }
                        }
                        return true;
                    default:
                        return false;
                }
            },
            [&](generic_type_instance const& /*type*/)
            {
                return false;
            },
            [&](fundamental_type const& type)
            {
                return (type != fundamental_type::String) && 
                    (type != fundamental_type::Char) &&
                    (type != fundamental_type::Boolean);
            },
            [&](auto&&)
            {
                return true;
            });
    }

    bool is_value_type(type_semantics const& semantics)
    {
        return call(semantics,
            [&](object_type)
            {
                return false;
            },
            [&](type_definition const& type)
            {
                switch (get_category(type))
                {
                    case category::enum_type:
                        return true;
                    case category::struct_type:
                        if (auto mapping = get_mapped_type(type.TypeNamespace(), type.TypeName()))
                        {
                            return true;
                        }

                        for (auto&& field : type.FieldList())
                        {
                            if (!is_value_type(get_type_semantics(field.Signature().Type())))
                            {
                                return false;
                            }
                        }
                        return true;
                    default:
                        return false;
                }
            },
            [&](generic_type_instance const& /*type*/)
            {
                return false;
            },
            [&](fundamental_type const& type)
            {
                return (type != fundamental_type::String);
            },
            [&](auto&&)
            {
                return true;
            });
    }

    void write_fundamental_type(writer& w, fundamental_type type)
    {
        w.write(to_csharp_type(type));
    }

    void write_projection_type(writer& w, type_semantics const& semantics);

    void write_generic_type_name_base(writer& w, uint32_t index)
    {
        write_projection_type(w, w.get_generic_arg_scope(index).first);
    }

    void write_generic_type_name(writer& w, uint32_t index)
    {
        w.write_generic_type_name_custom ?
            w.write_generic_type_name_custom(w, index) :
            write_generic_type_name_base(w, index);
    }

    template<typename TAction, typename TResult = std::invoke_result_t<TAction, type_definition>>
    TResult for_typedef(writer& w, type_semantics const& semantics, TAction action)
    {
        return call(semantics,
            [&](type_definition const& type)
            {
                return action(type);
            },
            [&](generic_type_instance const& type)
            {
                auto guard{ w.push_generic_args(type) };
                return action(type.generic_type);
            },
            [](auto)
            {
                throw_invalid("type definition expected");
                #pragma warning(disable:4702)
                return TResult();
            });
    }

    void write_typedef_name(writer& w, type_definition const& type, bool abiNamespace = false, bool forceWriteNamespace = false)
    {
        auto typeNamespace = type.TypeNamespace();
        auto typeName = type.TypeName();
        if (auto proj = get_mapped_type(typeNamespace, typeName))
        {
            typeNamespace = proj->mapped_namespace;
            typeName = proj->mapped_name;
        }

        if (forceWriteNamespace || ((typeNamespace != w._current_namespace) || (abiNamespace != w._in_abi_namespace)))
        {
            w.write("global::");
            if (abiNamespace)
            {
                w.write("ABI.");
            }

            w.write("%.", typeNamespace);
        }
        w.write("@", typeName);
    }

    void write_type_params(writer& w, TypeDef const& type)
    {
        if (distance(type.GenericParam()) == 0)
        {
            return;
        }
        separator s{ w };
        uint32_t index = 0;
        w.write("<%>", bind_each([&](writer& w, GenericParam const& /*gp*/)
            { s(); write_generic_type_name(w, index++); }, type.GenericParam()));
    }

    void write_type_name(writer& w, type_semantics const& semantics, bool abiNamespace = false, bool forceWriteNamespace = false)
    {
        for_typedef(w, semantics, [&](auto type)
        {
            write_typedef_name(w, type, abiNamespace, forceWriteNamespace);
            write_type_params(w, type);
        });
    }

    auto write_type_name_temp(writer& w, type_semantics const& type, char const* format = "%", bool abiNamespace = false)
    {
        return w.write_temp(format, bind<write_type_name>(type, abiNamespace, false));
    }

    void write_projection_type(writer& w, type_semantics const& semantics)
    {
        call(semantics,
            [&](object_type) { w.write("object"); },
            [&](guid_type) { w.write("Guid"); },
            [&](type_type) { w.write("Type"); },
            [&](type_definition const& type) { write_typedef_name(w, type); },
            [&](generic_type_index const& var) { write_generic_type_name(w, var.index); },
            [&](generic_type_instance const& type)
            {
                auto guard{ w.push_generic_args(type) };
                w.write("%<%>",
                    bind<write_projection_type>(type.generic_type),
                    bind_list<write_projection_type>(", ", type.generic_args));
            },
            [&](generic_type_param const& param) { w.write(param.Name()); },
            [&](fundamental_type const& type) { write_fundamental_type(w, type); });
    }

    bool is_keyword(std::string_view str)
    {
        static constexpr std::string_view keywords[] =
        {
            "abstract",  "as",       "base",     "bool",       "break",     "byte",
            "case",      "catch",    "char",     "checked",    "class",     "const",
            "continue",  "decimal",  "default",  "delegate",   "do",        "double",
            "else",      "enum",     "event",    "explicit",   "extern",    "false",
            "finally",   "fixed",    "float",    "for",        "foreach",   "goto",
            "if",        "implicit", "in",       "int",        "interface", "internal",
            "is",        "lock",     "long",     "namespace",  "new",       "null",
            "object",    "operator", "out",      "override",   "params",    "private",
            "protected", "public",   "readonly", "ref",        "return",    "sbyte",
            "sealed",    "short",    "sizeof",   "stackalloc", "static",    "string",
            "struct",    "switch",   "this",     "throw",      "true",      "try",
            "typeof",    "uint",     "ulong",    "unchecked",  "unsafe",    "ushort",
            "using",     "virtual",  "void",     "volatile",   "while"
        };
#if 0
        assert(std::is_sorted(std::begin(keywords), std::end(keywords)));
#endif
        return std::binary_search(std::begin(keywords), std::end(keywords), str);
    }

    void write_escaped_identifier(writer& w, std::string_view identifier)
    {
        if (is_keyword(identifier))
        {
            w.write("@");
        }
        w.write(identifier);
    }

    void write_parameter_name(writer& w, method_signature::param_t const& param)
    {
        write_escaped_identifier(w, param.first.Name());
    }

    void write_parameter_name_with_modifier(writer& w, method_signature::param_t const& param)
    {
        switch (get_param_category(param))
        {
        case param_category::out:
        case param_category::receive_array:
            w.write("out ");
            break;
        default:
            break;
        }
        write_parameter_name(w, param);
    }

    void write_projection_parameter_type(writer& w, method_signature::param_t const& param)
    {
        auto semantics = get_type_semantics(param.second->Type());

        switch (get_param_category(param))
        {
        case param_category::in:
            w.write("%", bind<write_projection_type>(semantics));
            break;
        case param_category::out:
            w.write("out %", bind<write_projection_type>(semantics));
            break;
        case param_category::pass_array:
        case param_category::fill_array:
            w.write("%[]", bind<write_projection_type>(semantics));
            break;
        case param_category::receive_array:
            w.write("out %[]", bind<write_projection_type>(semantics));
            break;
        }
    }

    void write_projected_signature(writer& w, TypeSig const& type_sig)
    {
        write_projection_type(w, get_type_semantics(type_sig));
        if(type_sig.is_szarray()) w.write("[]");
    };

    void write_projection_return_type(writer& w, method_signature const& signature)
    {
        if (auto return_sig = signature.return_signature())
        {
            write_projected_signature(w, return_sig.Type());
        }
        else
        {
            w.write("void");
        }
    }

    void write_projection_parameter(writer& w, method_signature::param_t const& param)
    {
        w.write("% %",
            bind<write_projection_parameter_type>(param),
            bind<write_parameter_name>(param));
    }

    void write_abi_type(writer& w, type_semantics const& semantics)
    {
        call(semantics,
            [&](object_type) { w.write("IntPtr"); },
            [&](guid_type) { w.write("Guid"); },
            [&](type_type) { throw_invalid("System.Type not implemented"); },
            [&](type_definition const& type)
            {
                switch (get_category(type))
                {
                    case category::enum_type:
                        write_type_name(w, type);
                        break;

                    case category::struct_type:
                        write_type_name(w, type, !is_type_blittable(semantics));
                        break;

                    default:
                        w.write("IntPtr");
                        break;
                };
            },
            [&](generic_type_index const& var)
            {
                write_generic_type_name(w, var.index);
            },
            [&](generic_type_instance const&)
            {
                w.write("IntPtr");
            },
            [&](generic_type_param const& param)
            {
                w.write(param.Name());
            },
            [&](fundamental_type type)
            {
                if (type == fundamental_type::String)
                {
                    w.write("IntPtr");
                }
                else
                {
                    if (type == fundamental_type::Boolean)
                    {
                        type = fundamental_type::UInt8;
                    }
                    if (type == fundamental_type::Char)
                    {
                        type = fundamental_type::UInt16;
                    }
                    write_fundamental_type(w, type);
                }
            });
    }

    void write_abi_parameter(writer& w, method_signature::param_t const& param)
    {
        auto semantics = get_type_semantics(param.second->Type());
        auto param_name = w.write_temp("%", bind<write_parameter_name>(param));
        switch (get_param_category(param))
        {
        case param_category::in:
            w.write(", % %", bind<write_abi_type>(semantics), param_name);
            break;
        case param_category::out:
            w.write(", out % %", bind<write_abi_type>(semantics), param_name);
            break;
        case param_category::pass_array:
        case param_category::fill_array:
            w.write(", int __%Size, IntPtr %", param_name, param_name);
            break;
        case param_category::receive_array:
            w.write(", out int __%Size, out IntPtr %", param_name, param_name);
            break;
        }
    }

    void write_abi_return(writer& w, method_signature const& signature)
    {
        if (auto return_sig = signature.return_signature())
        {
            auto semantics = get_type_semantics(return_sig.Type());
            auto return_param = w.write_temp("%", bind<write_escaped_identifier>(signature.return_param_name()));
            return_sig.Type().is_szarray() ?
                w.write(", out int __%Size, out IntPtr %", signature.return_param_name(), return_param) :
                w.write(", out % %", bind<write_abi_type>(semantics), return_param);
        }
    }

    void write_abi_parameters(writer& w, method_signature const& signature)
    {
        w.write("IntPtr thisPtr");
        for (auto&& param : signature.params())
        {
            write_abi_parameter(w, param);
        }
        write_abi_return(w, signature);
    }

    bool abi_signature_has_generic_parameters(writer& w, method_signature const& signature)
    {
        bool signature_has_generic_parameters{};

        writer::write_generic_type_name_guard g(w, [&](writer& /*w*/, uint32_t /*index*/) {
            signature_has_generic_parameters = true;
            });

        auto _ = w.write_temp("%", bind<write_abi_parameters>(signature));
        return signature_has_generic_parameters;
    }

    template<typename write_params>
    void write_event_params(writer& w, row_base<Event>::value_type const& evt, write_params params)
    {
        method_signature add_sig{ std::get<0>(get_event_methods(evt)) };
        auto semantics = get_type_semantics(add_sig.params().at(0).second->Type());

        if (auto td = std::get_if<type_definition>(&semantics))
        {
            method_signature invoke_sig{ get_delegate_invoke(*td) };
            if (invoke_sig.params().size() > 0)
            {
                params(w, invoke_sig);
            }
        }
        else if (auto gti = std::get_if<generic_type_instance>(&semantics))
        {
            auto guard{ w.push_generic_args(*gti) };
            method_signature invoke_sig{ get_delegate_invoke(gti->generic_type) };
            params(w, invoke_sig);
        }
    }

    void write_event_param_types(writer& w, row_base<Event>::value_type const& evt)
    {
        auto write_params = [](writer& w, method_signature const& invoke_sig)
        {
            w.write("<%>", bind_list<write_projection_parameter_type>(", ", invoke_sig.params()));
        };
        write_event_params(w, evt, write_params);
    }

    void write_delegate_abi_call(writer& w, TypeDef const& type, std::string_view call, std::string_view name)
    {
        w.write("%%.%(%)",
            bind<write_typedef_name>(type, true, false),
            bind<write_type_params>(type),
            call, name);
    }

    void write_object_marshal_from_abi(writer& w, type_semantics const& param_type, TypeDef const& type, std::string_view name, bool is_boxed = false)
    {
        switch (get_category(type))
        {
        case category::enum_type:
        {
            if (is_boxed)
            {
                w.write("(%)", bind<write_type_name>(type, false, false));
            }
            w.write("%", name);
            return;
        }
        case category::delegate_type:
        {
            write_delegate_abi_call(w, type, "FromAbi", name);
            return;
        }
        case category::struct_type:
        {
            if (is_type_blittable(param_type))
            {
                w.write("%", name);
            }
            else
            {
                w.write("%.FromAbi(%)", bind<write_type_name>(param_type, true, true), name);
            }
            return;
        }
        case category::interface_type:
        {
            w.write("MarshalInterface<%>.FromAbi(%)",
                bind<write_type_name>(type, false, false),
                name);
            return;
        }
        case category::class_type:
        {
            w.write("%.FromAbi(%)",
                bind<write_projection_type>(param_type),
                name);
            return;
        }
        }
    }

    void write_fundamental_marshal_to_abi(writer& w, fundamental_type type, std::string_view name)
    {
        switch (type)
        {
        case fundamental_type::String:
            w.write("%.Handle", name);
            break;
        case fundamental_type::Boolean:
            w.write("(byte)(% ? 1 : 0)", name);
            break;
        case fundamental_type::Char:
            w.write("(ushort)%", name);
            break;
        default:
            w.write("%", name);
            break;
        }
    }

    void write_fundamental_marshal_from_abi(writer& w, fundamental_type type, std::string_view name, bool is_boxed = false)
    {
        if (type == fundamental_type::String)
        {
            w.write(R"(MarshalString.FromAbi(%))", name);
        }
        else if (type == fundamental_type::Boolean)
        {
            w.write(is_boxed ? "((byte)(object)% != 0)" : "(% != 0)", name);
        }
        else if (type == fundamental_type::Char)
        {
            w.write(is_boxed ? "(char)(ushort)(object)%" : "(char)%", name);
        }
        else if (is_boxed)
        {
            w.write("(%)(object)%", bind<write_fundamental_type>(type), name);
        }
        else
        {
            w.write("%", name);
        }
    }

    void write_class_modifiers(writer& w, TypeDef const& type)
    {
        if (is_static(type))
        {
            w.write("static ");
            return;
        }

        if (type.Flags().Sealed())
        {
            w.write("sealed ");
        }
    }

    void write_method(writer& w, method_signature signature, std::string_view method_name,
        std::string_view return_type, std::string_view method_target,
        std::string_view access_spec = ""sv, std::string_view method_spec = ""sv)
    {
        w.write(R"(
%%% %(%) => %.%(%);
)",
            access_spec,
            method_spec,
            return_type,
            method_name,
            bind_list<write_projection_parameter>(", ", signature.params()),
            method_target,
            method_name,
            bind_list<write_parameter_name_with_modifier>(", ", signature.params())
        );
    }

    void write_explicitly_implemented_method(writer& w, MethodDef const& method,
        std::string_view return_type, TypeDef const& method_interface, std::string_view method_target)
    {
        method_signature signature{ method };
        w.write(R"(
% %.%(%) => %.%(%);
)",
            return_type,
            bind<write_type_name>(method_interface, false, false),
            method.Name(),
            bind_list<write_projection_parameter>(", ", signature.params()),
            method_target,
            method.Name(),
            bind_list<write_parameter_name_with_modifier>(", ", signature.params())
        );
    }

    void write_class_method(writer& w, MethodDef const& method, TypeDef const& class_type, bool is_overridable, bool is_protected, std::string_view interface_member)
    {
        if (method.SpecialName())
        {
            return;
        }

        auto access_spec = is_protected || is_overridable ? "protected " : "public ";
        std::string method_spec = "";

        // If this interface is overridable but the type is sealed, don't mark the member as virtual.
        // The C# compiler errors out about declaring a virtual member in a sealed class.
        if (is_overridable && !class_type.Flags().Sealed())
        {
            // All overridable methods in the WinRT type system have protected visibility.
            access_spec = "protected ";
            method_spec = "virtual ";
        }

        method_signature signature{ method };

        auto raw_return_type = w.write_temp("%", [&](writer& w) {
            write_projection_return_type(w, signature);
        });
        auto return_type = raw_return_type;
        if (method.Name() == "ToString")
        {
            method_spec += "new ";
            if (signature.params().empty())
            {
                if (auto ret = signature.return_signature())
                {
                    auto semantics = get_type_semantics(ret.Type());
                    if (auto ft = std::get_if<fundamental_type>(&semantics))
                    {
                        if (*ft == fundamental_type::String)
                        {
                            method_spec = "override ";
                            return_type = "string";
                        }
                    }
                }
            }
        }

        write_method(w, signature, method.Name(), return_type, interface_member, access_spec, method_spec);

        if (is_overridable || !is_exclusive_to(method.Parent()))
        {
            w.write(R"(
% %.%(%) => %(%);)",
                bind<write_projection_return_type>(signature),
                bind<write_type_name>(method.Parent(), false, false),
                method.Name(),
                bind_list<write_projection_parameter>(", ", signature.params()),
                method.Name(),
                bind_list<write_parameter_name_with_modifier>(", ", signature.params())
            );
        }
    }

    void write_property(writer& w, std::string_view external_prop_name, std::string_view prop_name,
        std::string_view prop_type, std::string_view getter_target, std::string_view setter_target,
        std::string_view access_spec = ""sv, std::string_view method_spec = ""sv)
    {
        if (setter_target.empty())
        {
            w.write(R"(
%%% % => %.%;
)",
                access_spec,
                method_spec,
                prop_type,
                external_prop_name,
                getter_target,
                prop_name);
        }
        else
        {
            w.write(R"(
%%% %
{
get => %.%;
set => %.% = value;
}
)",
                access_spec,
                method_spec,
                prop_type,
                external_prop_name,
                getter_target,
                prop_name,
                setter_target,
                prop_name);
        }
    }

    std::string write_as_cast(writer& w, TypeDef const& iface, bool as_abi)
    {
        return w.write_temp(as_abi ? "As<%>()" : "AsInternal(new InterfaceTag<%>())",
            bind<write_type_name>(iface, as_abi, false));
    }

    void write_lazy_interface_initialization(writer& w, TypeDef const& type)
    {
        for (auto&& ii : type.InterfaceImpl())
        {
            if (has_attribute(ii, "Windows.Foundation.Metadata", "DefaultAttribute"))
            {
                continue;
            }

            for_typedef(w, get_type_semantics(ii.Interface()), [&](auto interface_type)
            {
                auto interface_name = write_type_name_temp(w, interface_type);
                auto interface_abi_name = write_type_name_temp(w, interface_type, "%", true);

                w.write(R"(
{typeof(%), new Lazy<%>(() => new %(GetReferenceForQI()))},)",
                    interface_name,
                    interface_abi_name,
                    interface_abi_name);
            });
        }
    }

    std::string write_explicit_name(writer& w, TypeDef const& iface, std::string_view name)
    {
        return w.write_temp("%.%", write_type_name_temp(w, iface), name);
    }

    std::string write_prop_type(writer& w, Property const& prop)
    {
        return w.write_temp("%", bind<write_projected_signature>(prop.Type().Type()));
    }

    void write_explicitly_implemented_property(writer& w, Property const& prop, TypeDef const& iface, bool as_abi)
    {
        auto prop_target = write_as_cast(w, iface, as_abi);
        auto [getter, setter] = get_property_methods(prop);
        auto getter_target = getter ? prop_target : "";
        auto setter_target = setter ? prop_target : "";
        write_property(w, write_explicit_name(w, iface, prop.Name()), prop.Name(),
            write_prop_type(w, prop), getter_target, setter_target);
    }

    void write_event(writer& w, std::string_view external_event_name, Event const& event, std::string_view event_target,
        std::string_view access_spec = ""sv, std::string_view method_spec = ""sv)
    {
        auto event_type = w.write_temp("%", bind<write_type_name>(get_type_semantics(event.EventType()), false, false));

        // ICommand has a lower-fidelity type mapping where the type of the event handler doesn't project one-to-one
        // so we need to hard-code mapping the event handler from the mapped WinRT type to the correct .NET type.
        if (event.Name() == "CanExecuteChanged" && event_type == "global::System.EventHandler<object>")
        {
            event_type = "global::System.EventHandler";
        }

        w.write(R"(
%%event % %
{
add => %.% += value;
remove => %.% -= value;
}
)",
            access_spec,
            method_spec,
            event_type,
            external_event_name,
            event_target,
            event.Name(),
            event_target,
            event.Name());
    }

    void write_explicitly_implemented_event(writer& w, Event const& evt, TypeDef const& iface, bool as_abi)
    {
        write_event(w, write_explicit_name(w, iface, evt.Name()), evt, write_as_cast(w, iface, as_abi));
    }

    void write_class_event(writer& w, Event const& event, bool is_overridable, bool is_protected, std::string_view interface_member)
    {
        auto visibility = "public ";

        if (is_protected)
        {
            visibility = "protected ";
        }

        if (is_overridable)
        {
            visibility = "protected virtual ";
        }
        write_event(w, event.Name(), event, interface_member, visibility);

        if (is_overridable || !is_exclusive_to(event.Parent()))
        {
            write_event(w, w.write_temp("%.%", bind<write_type_name>(event.Parent(), false, false), event.Name()), event, "this");
        }
    }

    struct attributed_type
    {
        TypeDef type;
        bool activatable{};
        bool statics{};
        bool composable{};
        bool visible{};
    };

    static auto get_attributed_types(writer& w, TypeDef const& type)
    {
        auto get_system_type = [&](auto&& signature) -> TypeDef
        {
            for (auto&& arg : signature.FixedArgs())
            {
                if (auto type_param = std::get_if<ElemSig::SystemType>(&std::get<ElemSig>(arg.value).value))
                {
                    return type.get_cache().find_required(type_param->name);
                }
            }

            return {};
        };

        std::map<std::string, attributed_type> result;

        for (auto&& attribute : type.CustomAttribute())
        {
            auto attribute_name = attribute.TypeNamespaceAndName();

            if (attribute_name.first != "Windows.Foundation.Metadata")
            {
                continue;
            }

            auto signature = attribute.Value();
            attributed_type info;

            if (attribute_name.second == "ActivatableAttribute")
            {
                info.type = get_system_type(signature);
                info.activatable = true;
            }
            else if (attribute_name.second == "StaticAttribute")
            {
                info.type = get_system_type(signature);
                info.statics = true;
            }
            else if (attribute_name.second == "ComposableAttribute")
            {
                info.type = get_system_type(signature);
                info.composable = true;

                for (auto&& arg : signature.FixedArgs())
                {
                    if (auto visibility = std::get_if<ElemSig::EnumValue>(&std::get<ElemSig>(arg.value).value))
                    {
                        info.visible = std::get<int32_t>(visibility->value) == 2;
                        break;
                    }
                }
            }
            else
            {
                continue;
            }

            std::string name;

            if (info.type)
            {
                name = w.write_temp("%", info.type.TypeName());
            }

            result[name] = std::move(info);
        }

        return result;
    }

    void write_composing_factory_method(writer& w, MethodDef const& method);

    void write_abi_method_with_raw_return_type(writer& w, MethodDef const& method);

    template<auto method_writer>
    std::string write_factory_cache_object(writer& w, TypeDef const& factory_type, TypeDef const& class_type);

    std::string write_static_cache_object(writer& w, std::string_view cache_type_name, TypeDef const& class_type)
    {
        auto cache_interface =
            w.write_temp(
                R"((new BaseActivationFactory("%", "%.%"))._As<ABI.%.%.Vftbl>)",
                class_type.TypeNamespace(),
                class_type.TypeNamespace(),
                class_type.TypeName(),
                class_type.TypeNamespace(),
                cache_type_name);

        w.write(R"(
internal class _% : ABI.%.%
{
public _%() : base(%()) { }
private static WeakLazy<_%> _instance = new WeakLazy<_%>();
internal static % Instance => _instance.Value;
}
)",
            cache_type_name,
            class_type.TypeNamespace(),
            cache_type_name,
            cache_type_name,
            cache_interface,
            cache_type_name,
            cache_type_name,
            cache_type_name);

        return w.write_temp("_%.Instance", cache_type_name);
    }

    static std::string get_default_interface_name(writer& w, TypeDef const& type, bool abiNamespace = true)
    {
        return w.write_temp("%", bind<write_type_name>(get_type_semantics(get_default_interface(type)), abiNamespace, false));
    }

    void write_factory_constructors(writer& w, TypeDef const& factory_type, TypeDef const& class_type)
    {
        auto default_interface_name = get_default_interface_name(w, class_type);
        if (factory_type)
        {
            auto cache_object = write_factory_cache_object<write_abi_method_with_raw_return_type>(w, factory_type, class_type);

            for (auto&& method : factory_type.MethodList())
            {
                method_signature signature{ method };
                w.write(R"(
public %(%) : this(((Func<%>)(() => {
IntPtr ptr = (%.%(%));
try
{
return new %(ComWrappersSupport.GetObjectReferenceForInterface(ptr));
}
finally
{
MarshalInspectable.DisposeAbi(ptr);
}
}))())
{
    ComWrappersSupport.RegisterObjectForInterface(this, ThisPtr);
}
)",
                    class_type.TypeName(),
                    bind_list<write_projection_parameter>(", ", signature.params()),
                    default_interface_name,
                    cache_object,
                    method.Name(),
                    bind_list<write_parameter_name_with_modifier>(", ", signature.params()),
                    default_interface_name);
            }
        }
        else
        {
            w.write(R"(
public %() : this(new %(ActivationFactory<%>.ActivateInstance<%.Vftbl>()))
{
ComWrappersSupport.RegisterObjectForInterface(this, ThisPtr);
}
)",
                class_type.TypeName(),
                default_interface_name,
                class_type.TypeName(),
                default_interface_name);
        }
    }

    void write_composable_constructors(writer& w, TypeDef const& composable_type, TypeDef const& class_type, std::string_view visibility)
    {
        auto cache_object = write_factory_cache_object<write_composing_factory_method>(w, composable_type, class_type);
        auto default_interface_name = get_default_interface_name(w, class_type);

        for (auto&& method : composable_type.MethodList())
        {
            method_signature signature{ method };
            bool has_base_type = !std::holds_alternative<object_type>(get_type_semantics(class_type.Extends()));
            auto params_without_objects = signature.params();
            params_without_objects.pop_back();
            params_without_objects.pop_back();

            w.write(R"(
% %(%)%
{
object baseInspectable = this.GetType() != typeof(%) ? this : null;
IntPtr composed = %.%(%%baseInspectable, out IntPtr ptr);
using IObjectReference composedRef = ObjectReference<IUnknownVftbl>.Attach(ref composed);
try
{
_inner = ComWrappersSupport.GetObjectReferenceForInterface(ptr);
var defaultInterface = new %(_inner);
_defaultLazy = new Lazy<%>(() => defaultInterface);

ComWrappersSupport.RegisterObjectForInterface(this, ThisPtr);
}
finally
{
MarshalInspectable.DisposeAbi(ptr);
}
}
)",
                visibility,
                class_type.TypeName(),
                bind_list<write_projection_parameter>(", ", params_without_objects),
                has_base_type ? ":base(global::WinRT.DerivedComposed.Instance)" : "",
                bind<write_type_name>(class_type, false, false),
                cache_object,
                method.Name(),
                bind_list<write_parameter_name_with_modifier>(", ", params_without_objects),
                [&](writer& w) {w.write("%", params_without_objects.empty() ? " " : ", "); },
                default_interface_name,
                default_interface_name);
        }
    }

    void write_static_method(writer& w, MethodDef const& method, std::string_view method_target)
    {
        if (method.SpecialName())
        {
            return;
        }
        method_signature signature{ method };
        auto return_type = w.write_temp("%", [&](writer& w) {
            write_projection_return_type(w, signature);
        });
        write_method(w, signature, method.Name(), return_type, method_target, "public "sv, "static "sv);
    }

    void write_static_property(writer& w, Property const& prop, std::string_view prop_target)
    {
        auto [getter, setter] = get_property_methods(prop);
        auto getter_target = getter ? prop_target : "";
        auto setter_target = setter ? prop_target : "";
        write_property(w, prop.Name(), prop.Name(), write_prop_type(w, prop),
            getter_target, setter_target, "public "sv, "static "sv);
    }

    void write_static_event(writer& w, Event const& event, std::string_view event_target)
    {
        write_event(w, event.Name(), event, event_target, "public "sv, "static "sv);
    }

    void write_static_members(writer& w, TypeDef const& static_type, TypeDef const& class_type)
    {
        auto cache_object = write_static_cache_object(w, static_type.TypeName(), class_type);
        w.write_each<write_static_method>(static_type.MethodList(), cache_object);
        w.write_each<write_static_property>(static_type.PropertyList(), cache_object);
        w.write_each<write_static_event>(static_type.EventList(), cache_object);
    }

    void write_attributed_types(writer& w, TypeDef const& type)
    {
        for (auto&& [interface_name, factory] : get_attributed_types(w, type))
        {
            if (factory.activatable)
            {
                write_factory_constructors(w, factory.type, type);
            }
            else if (factory.composable)
            {
                write_composable_constructors(w, factory.type, type, factory.visible ? "public"sv : "protected"sv);
            }
            else if (factory.statics)
            {
                write_static_members(w, factory.type, type);
            }
        }
    }

    void write_nongeneric_enumerable_members(writer& w, std::string_view target)
    {
        w.write(R"(
IEnumerator IEnumerable.GetEnumerator() => %.GetEnumerator();
)",
            target);
    }

    void write_enumerable_members(writer& w, std::string_view target, bool include_nongeneric)
    {
        auto element = w.write_temp("%", bind<write_generic_type_name>(0));
        w.write(R"(
public IEnumerator<%> GetEnumerator() => %.GetEnumerator();
)",         
            element, target);

        if (!include_nongeneric) return;
        w.write(R"(
IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
)");
    }

    void write_enumerator_members(writer& w, std::string_view target)
    {
        auto element = w.write_temp("%", bind<write_generic_type_name>(0));
        w.write(R"(
public bool MoveNext() => %.MoveNext();
public void Reset() => %.Reset();
public void Dispose() => %.Dispose();
public % Current => %.Current;
object IEnumerator.Current => Current;
)", 
            target, 
            target, 
            target, 
            element, target);
    }

    void write_readonlydictionary_members(writer& w, std::string_view target, bool include_enumerable)
    {
        auto key = w.write_temp("%", bind<write_generic_type_name>(0));
        auto value = w.write_temp("%", bind<write_generic_type_name>(1));
        w.write(R"(
public IEnumerable<%> Keys => %.Keys;
public IEnumerable<%> Values => %.Values;
public int Count => %.Count;
public % this[% key] => %[key];
public bool ContainsKey(% key) => %.ContainsKey(key);
public bool TryGetValue(% key, out % value) => %.TryGetValue(key, out value);
)", 
            key, target, 
            value, target, 
            target, 
            value, key, target, 
            key, target,
            key, value, target);
        
        if (!include_enumerable) return;
        w.write(R"(
public IEnumerator<KeyValuePair<%, %>> GetEnumerator() => %.GetEnumerator();
IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
)",
            key, value, target);
    }

    void write_dictionary_members(writer& w, std::string_view target, bool include_enumerable)
    {
        auto key = w.write_temp("%", bind<write_generic_type_name>(0));
        auto value = w.write_temp("%", bind<write_generic_type_name>(1));
        w.write(R"(
public ICollection<%> Keys => %.Keys;
public ICollection<%> Values => %.Values;
public int Count => %.Count;
public bool IsReadOnly => %.IsReadOnly;
public % this[% key] 
{
get => %[key];
set => %[key] = value;
}
public void Add(% key, % value) => %.Add(key, value);
public bool ContainsKey(% key) => %.ContainsKey(key);
public bool Remove(% key) => %.Remove(key);
public bool TryGetValue(% key, out % value) => %.TryGetValue(key, out value);
public void Add(KeyValuePair<%, %> item) => %.Add(item);
public void Clear() => %.Clear();
public bool Contains(KeyValuePair<%, %> item) => %.Contains(item);
public void CopyTo(KeyValuePair<%, %>[] array, int arrayIndex) => %.CopyTo(array, arrayIndex);
bool ICollection<KeyValuePair<%, %>>.Remove(KeyValuePair<%, %> item) => %.Remove(item);
)", 
            key, target, 
            value, target, 
            target, 
            target, 
            value, key, target, target, 
            key, value, target, 
            key, target, 
            key, target, 
            key, value, target,
            key, value, target,
            target,
            key, value, target,
            key, value, target,
            key, value, key, value, target);
        
        if (!include_enumerable) return;
        w.write(R"(
public IEnumerator<KeyValuePair<%, %>> GetEnumerator() => %.GetEnumerator();
IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
)",
            key, value, target);
    }

    void write_readonlylist_members(writer& w, std::string_view target, bool include_enumerable)
    {
        auto element = w.write_temp("%", bind<write_generic_type_name>(0));
        w.write(R"(
public int Count => %.Count;
[global::System.Runtime.CompilerServices.IndexerName("ReadOnlyListItem")]
public % this[int index] => %[index];
)",
            target,
            element, target);
        
        if (!include_enumerable) return;
        w.write(R"(
public IEnumerator<%> GetEnumerator() => %.GetEnumerator();
IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
)",
            element, target);
    }

    void write_nongeneric_list_members(writer& w, std::string_view target, bool include_enumerable)
    {
        w.write(R"(
public int Count => %.Count;
public bool IsSynchronized => %.IsSynchronized;
public object SyncRoot => %.SyncRoot;
public void CopyTo(Array array, int index) => %.CopyTo(array, index);
[global::System.Runtime.CompilerServices.IndexerName("NonGenericListItem")]
public object this[int index]
{
get => %[index];
set => %[index] = value;
}
public bool IsFixedSize => %.IsFixedSize;
public bool IsReadOnly => %.IsReadOnly;
public int Add(object value) => %.Add(value);
public void Clear() => %.Clear();
public bool Contains(object value) => %.Contains(value);
public int IndexOf(object value) => %.IndexOf(value);
public void Insert(int index, object value) => %.Insert(index, value);
public void Remove(object value) => %.Remove(value);
public void RemoveAt(int index) => %.RemoveAt(index);
)", 
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target,
            target, 
            target);
        
        if (!include_enumerable) return;
        w.write(R"(
IEnumerator IEnumerable.GetEnumerator() => %.GetEnumerator();
)",
            target);
    }

    void write_list_members(writer& w, std::string_view target, bool include_enumerable)
    {
        auto element = w.write_temp("%", bind<write_generic_type_name>(0));
        w.write(R"(
public int Count => %.Count;
public bool IsReadOnly => %.IsReadOnly;
[global::System.Runtime.CompilerServices.IndexerName("ListItem")]
public % this[int index] 
{
get => %[index];
set => %[index] = value;
}
public int IndexOf(% item) => %.IndexOf(item);
public void Insert(int index, % item) => %.Insert(index, item);
public void RemoveAt(int index) => %.RemoveAt(index);
public void Add(% item) => %.Add(item);
public void Clear() => %.Clear();
public bool Contains(% item) => %.Contains(item);
public void CopyTo(%[] array, int arrayIndex) => %.CopyTo(array, arrayIndex);
public bool Remove(% item) => %.Remove(item);
)", 
            target, 
            target, 
            element, target, target, 
            element, target,
            element, target,
            target, 
            element, target,
            target, 
            element, target,
            element, target,
            element, target);
        
        if (!include_enumerable) return;
        w.write(R"(
public IEnumerator<%> GetEnumerator() => %.GetEnumerator();
IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
)",
            element, target);
    }

    void write_idisposable_members(writer& w, std::string_view target)
    {
        w.write(R"(
public void Dispose() => %.Dispose();
)",
target);
    }

    void write_custom_mapped_type_members(writer& w, std::string_view target, mapped_type const& mapping)
    {
        if (mapping.abi_name == "IIterable`1") 
        {
            write_enumerable_members(w, target, true);
        }
        else if (mapping.abi_name == "IIterator`1") 
        {
            write_enumerator_members(w, target);
        }
        else if (mapping.abi_name == "IMapView`2") 
        {
            write_readonlydictionary_members(w, target, false);
        }
        else if (mapping.abi_name == "IMap`2") 
        {
            write_dictionary_members(w, target, false);
        }
        else if (mapping.abi_name == "IVectorView`1")
        {
            write_readonlylist_members(w, target, false);
        }
        else if (mapping.abi_name == "IVector`1")
        {
            write_list_members(w, target, false);
        }
        else if (mapping.abi_name == "IBindableIterable")
        {
            write_nongeneric_enumerable_members(w, target);
        }
        else if (mapping.abi_name == "IBindableVector")
        {
            write_nongeneric_list_members(w, target, false);
        }
        else if (mapping.mapped_namespace == "System" && mapping.mapped_name == "IDisposable")
        {
            write_idisposable_members(w, target);
        }
    }

    std::pair<std::string, bool> find_property_interface(writer& w, TypeDef const& setter_iface, std::string_view prop_name)
    {
        std::string getter_iface;

        auto search_interface = [&](TypeDef const& type)
        {
            for (auto&& prop : type.PropertyList())
            {
                if (prop.Name() == prop_name)
                {
                    getter_iface = write_type_name_temp(w, type, "%", true);
                    return true;
                }
            }
            return false;
        };

        std::function<bool(TypeDef const&)> search_interfaces = [&](TypeDef const& type)
        {
            for (auto&& iface : type.InterfaceImpl())
            {
                auto semantics = get_type_semantics(iface.Interface());
                if (for_typedef(w, semantics, [&](auto&& type)
                    {
                        return (setter_iface != type) && (search_interface(type) || search_interfaces(type));
                    })) {
                    return true;
                }
            }
            return false;
        };

        // first search base interfaces for property getter
        if (search_interfaces(setter_iface))
        {
            return { getter_iface, true };
        }

        // then search peer exclusive-to interfaces and their bases
        if (auto exclusive_to_attr = get_attribute(setter_iface, "Windows.Foundation.Metadata", "ExclusiveToAttribute"))
        {
            auto sig = exclusive_to_attr.Value();
            auto const& fixed_args = sig.FixedArgs();
            XLANG_ASSERT(fixed_args.size() == 1);
            auto sys_type = std::get<ElemSig::SystemType>(std::get<ElemSig>(fixed_args[0].value).value);
            auto exclusive_to_type = setter_iface.get_cache().find_required(sys_type.name);
            if (search_interfaces(exclusive_to_type))
            {
                return { getter_iface, false };
            }
        }

        throw_invalid("Could not find property getter interface");
    }

    void write_class_members(writer& w, TypeDef const& type)
    {
        std::map<std::string_view, std::tuple<std::string, std::string, std::string, bool, bool>> properties;
        for (auto&& ii : type.InterfaceImpl())
        {
            auto semantics = get_type_semantics(ii.Interface());

            auto write_class_interface = [&](TypeDef const& interface_type)
            {
                auto interface_name = write_type_name_temp(w, interface_type);
                auto interface_abi_name = write_type_name_temp(w, interface_type, "%", true);

                auto is_default_interface = has_attribute(ii, "Windows.Foundation.Metadata", "DefaultAttribute");
                auto target = is_default_interface ? "_default" : write_type_name_temp(w, interface_type, "AsInternal(new InterfaceTag<%>())");
                if (!is_default_interface)
                {
                    w.write(R"(
private % AsInternal(InterfaceTag<%> _) => ((Lazy<%>)_lazyInterfaces[typeof(%)]).Value;
)",
                        interface_name,
                        interface_name,
                        interface_abi_name,
                        interface_name);
                }

                if(auto mapping = get_mapped_type(interface_type.TypeNamespace(), interface_type.TypeName()); mapping && mapping->has_custom_members_output)
                {
                    write_custom_mapped_type_members(w, target, *mapping);
                    return;
                }

                auto is_overridable_interface = has_attribute(ii, "Windows.Foundation.Metadata", "OverridableAttribute");
                auto is_protected_interface = has_attribute(ii, "Windows.Foundation.Metadata", "ProtectedAttribute");

                w.write_each<write_class_method>(interface_type.MethodList(), type, is_overridable_interface, is_protected_interface, target);
                w.write_each<write_class_event>(interface_type.EventList(), is_overridable_interface, is_protected_interface, target);

                // Merge property getters/setters, since such may be defined across interfaces
                // Since a property has to either be overridable or not,
                for (auto&& prop : interface_type.PropertyList())
                {
                    MethodDef getter, setter;
                    std::tie(getter, setter) = get_property_methods(prop);
                    auto prop_type = write_prop_type(w, prop);
                    auto [prop_targets, inserted]  = properties.try_emplace(prop.Name(),
                        prop_type,
                        getter ? target : "",
                        setter ? target : "",
                        is_overridable_interface,
                        !is_protected_interface && !is_overridable_interface // By default, an overridable member is protected.
                        );
                    if (!inserted)
                    {
                        auto& [property_type, getter_target, setter_target, is_overridable, is_public] = prop_targets->second;
                        XLANG_ASSERT(property_type == prop_type);
                        if (getter)
                        {
                            XLANG_ASSERT(getter_target.empty());
                            getter_target = target;
                        }
                        if (setter)
                        {
                            XLANG_ASSERT(setter_target.empty());
                            setter_target = target;
                        }
                        is_overridable |= is_overridable_interface;
                        is_public |= !is_overridable_interface && !is_protected_interface;
                        XLANG_ASSERT(!getter_target.empty() || !setter_target.empty());
                    }

                    // If this interface is overridable then we need to emit an explicit implementation of the property for that interface.
                    if (is_overridable_interface || !is_exclusive_to(interface_type))
                    {
                        w.write("% %.% {%%}",
                            prop_type,
                            bind<write_type_name>(interface_type, false, false),
                            prop.Name(),
                            bind([&](writer& w)
                            {
                                if (getter || find_property_interface(w, interface_type, prop.Name()).second)
                                {
                                    w.write("get => %; ", prop.Name());
                                }
                            }),
                            bind([&](writer& w)
                            {
                                if (setter)
                                {
                                    w.write("set => % = value; ", prop.Name());
                                }
                            }));
                    }
                }
            };
            for_typedef(w, semantics, [&](auto type)
            {
                write_class_interface(type);
            });
        }

        // Write properties with merged accessors
        for (auto& [prop_name, prop_data] : properties)
        {
            auto& [prop_type, getter_target, setter_target, is_overridable, is_public] = prop_data;
            std::string_view access_spec = is_public ? "public "sv : "protected "sv;
            std::string_view method_spec = is_overridable ? "virtual "sv : ""sv;
            write_property(w, prop_name, prop_name, prop_type, getter_target, setter_target, access_spec, method_spec);
        }
    }

    void write_static_class(writer& w, TypeDef const& type)
    {
        w.write(R"(public static class %
{
%})",
            bind<write_type_name>(type, false, false),
            bind<write_attributed_types>(type)
        );
    }

    void write_event_source_ctors(writer& w, TypeDef const& type)
    {
        uint32_t const vtable_base = type.MethodList().first.index();
        for (auto&& evt : type.EventList())
        {
            auto [add, remove] = get_event_methods(evt);
            w.write(R"(

_% =
    new EventSource<%>(_obj,
    _obj.Vftbl.%,
    _obj.Vftbl.%);)",
                evt.Name(),
                bind<write_type_name>(get_type_semantics(evt.EventType()), false, false),
                get_vmethod_name(w, type, add),
                get_vmethod_name(w, type, remove));
        }
    }

    void write_event_sources(writer& w, TypeDef const& type)
    {
        for (auto&& evt : type.EventList())
        {
            w.write(R"(
private EventSource<%> _%;)",
                bind<write_type_name>(get_type_semantics(evt.EventType()), false, false),
                evt.Name());
        }
    }

    void write_interface_member_signatures(writer& w, TypeDef const& type)
    {
        for (auto&& method : type.MethodList())
        {
            if (is_special(method))
            {
                continue;
            }

            method_signature signature{ method };
            w.write(R"(
% %(%);)",
                bind<write_projection_return_type>(signature),
                method.Name(),
                bind_list<write_projection_parameter>(", ", signature.params())
            );
        }

        for (auto&& prop : type.PropertyList())
        {
            auto [getter, setter] = get_property_methods(prop);
            // "new" required if overriding a getter in a base interface
            auto new_keyword = (!getter && setter && find_property_interface(w, type, prop.Name()).second) ? "new " : "";
            w.write(R"(
%% % {%% })",
                new_keyword,
                write_prop_type(w, prop),
                prop.Name(),
                getter || setter ? " get;" : "",
                setter ? " set;" : ""
            );
        }

        for (auto&& evt : type.EventList())
        {
            w.write(R"(
event % %;)",
                bind<write_type_name>(get_type_semantics(evt.EventType()), false, false),
                evt.Name());
        }
    }

    struct abi_marshaler
    {
        std::string param_name;
        int param_index;
        param_category category;
        bool is_return;
        std::string param_type;
        std::string local_type;
        std::string marshaler_type;
        bool is_value_type;

        bool is_out() const
        {
            return (category == param_category::out) ||
                (category == param_category::receive_array);
        }

        bool is_ref() const
        {
            return (category == param_category::fill_array);
        }

        bool is_generic() const
        {
            return param_index > -1;
        }

        bool is_array() const
        {
            return category >= param_category::pass_array;
        }

        bool is_object_in() const
        {
            return (category == param_category::in) &&
                marshaler_type.empty() && local_type == "IntPtr";
        }

        std::string get_marshaler_local(writer& w) const
        {
            return w.write_temp("__%", param_name);
        }

        std::string get_param_local(writer& w) const
        {
            if (!is_generic())
            {
                return is_array() ?
                    w.write_temp("(__%_length, __%_data)",
                        param_name, param_name) :
                    get_marshaler_local(w);
            }
            return is_array() ?
                w.write_temp("(__params[%], __params[%])",
                    param_index, param_index + 1) :
                w.write_temp("__params[%]", param_index);
        }

        void write_locals(writer& w) const
        {
            if (is_generic())
            {
                if (!is_out() && !marshaler_type.empty())
                {
                    w.write("% __% = default;\n", local_type, param_name);
                }
                return;
            }

            if (is_object_in() || local_type.empty())
                return;

            if (!is_array() || !is_out())
            {
                w.write("% __% = default;\n",
                    local_type,
                    param_name);
            }

            if (is_array())
            {
                w.write("int __%_length = default;\n", param_name);
                w.write("IntPtr __%_data = default;\n", param_name);
            }
        }

        void write_create(writer& w, std::string_view source) const
        {
            w.write("%.CreateMarshaler%(%)",
                marshaler_type,
                is_array() ? "Array" : "",
                source);
        }

        auto get_escaped_param_name(writer& w) const
        {
            return w.write_temp("%", bind<write_escaped_identifier>(param_name));
        }

        void write_assignments(writer& w) const
        {
            if (is_object_in() || is_out() || local_type.empty())
                return;

            w.write("% = %.CreateMarshaler%(%);\n",
                get_marshaler_local(w),
                marshaler_type,
                is_array() ? "Array" : "",
                bind<write_escaped_identifier>(param_name));

            if (is_generic() || is_array())
            {
                w.write("% = %.GetAbi%(%);\n",
                    get_param_local(w),
                    marshaler_type,
                    is_array() ? "Array" : "",
                    get_marshaler_local(w));
            }
        }

        void write_marshal_to_abi(writer& w, std::string_view source = "") const
        {
            if (!is_generic())
            {
                if (is_array())
                {
                    w.write("%__%_length, %__%_data",
                        is_out() ? "out " : "", param_name,
                        is_out() ? "out " : "", param_name);
                    return;
                }

                if (is_out())
                {
                    w.write("out __%", param_name);
                    return;
                }

                if (is_object_in())
                {
                    w.write("%%.ThisPtr", source, bind<write_escaped_identifier>(param_name));
                    return;
                }

                if (marshaler_type.empty())
                {
                    if (param_type == "bool")
                    {
                        w.write("(byte)(%% ? 1 : 0)",
                            source, bind<write_escaped_identifier>(param_name));
                        return;
                    }
                    if (param_type == "char")
                    {
                        w.write("(ushort)%%",
                            source, bind<write_escaped_identifier>(param_name));
                        return;
                    }
                    w.write("%%",
                        source, bind<write_escaped_identifier>(param_name));
                    return;
                }
            }

            if (is_array())
            {
                w.write("__%_length, __%_data",
                    param_name,
                    param_name);
                return;
            }

            if (marshaler_type.empty())
            {
                write_escaped_identifier(w, param_name);
                return;
            }

            w.write("%.GetAbi%(%)",
                marshaler_type,
                is_array() ? "Array" : "",
                get_marshaler_local(w));
        }

        void write_from_abi(writer& w, std::string_view source) const
        {
            auto param_cast = is_generic() ?
                w.write_temp("(%)", param_type) : "";

            if (marshaler_type.empty())
            {
                if (local_type == "IntPtr" && param_type != "IntPtr")
                {
                    w.write("%.FromAbi(%)", param_type, source);
                    return;
                }
                if (param_type == "bool")
                {
                    w.write(is_generic() ? "(byte)% != 0" : "% != 0", source);
                    return;
                }
                if (param_type == "char")
                {
                    w.write(is_generic() ? "(char)(ushort)%" : "(char)%", source);
                    return;
                }
                w.write("%%", param_cast, source);
                return;
            }

            w.write("%.FromAbi%(%)",
                marshaler_type,
                is_array() ? "Array" : "",
                source);
        }

        void write_from_managed(writer& w, std::string_view source) const
        {
            auto param_cast = is_generic() ?
                w.write_temp("(%)", param_type) : "";

            if (marshaler_type.empty())
            {
                if (local_type == "IntPtr")
                {
                    w.write("%.FromManaged(%)", param_type, source);
                    return;
                }
                if (param_type == "bool")
                {
                    w.write("(byte)(% ? 1 : 0)", source);
                    return;
                }
                if (param_type == "char")
                {
                    w.write("(ushort)%", source);
                    return;
                }
                w.write("%%", param_cast, source);
                return;
            }

            w.write("%.FromManaged%(%)",
                marshaler_type,
                is_array() ? "Array" : "",
                source);
        }

        void write_marshal_from_abi(writer& w) const
        {
            if (!is_ref() && (!is_out() || local_type.empty()))
                return;
            if (is_ref())
            {
                if (!starts_with(marshaler_type, "MarshalBlittable"))
                {
                    w.write("%.CopyAbiArray(%, (__%_length, __%_data));\n",
                        marshaler_type,
                        bind<write_escaped_identifier>(param_name),
                        param_name,
                        param_name);
                }
                return;
            }
            is_return ?
                w.write("return ") :
                w.write("% = ", bind<write_escaped_identifier>(param_name));
            write_from_abi(w, get_param_local(w));
            w.write(";\n");
        }

        void write_dispose(writer& w) const
        {
            if (is_object_in() || local_type.empty())
                return;

            if (marshaler_type.empty())
            {
                if (is_out() && (local_type == "IntPtr" && param_type != "IntPtr"))
                {
                    w.write("MarshalInspectable.DisposeAbi(%);\n", get_marshaler_local(w));
                }
                return;
            }

            if (is_out())
            {
                w.write("%.DisposeAbi%(%);\n",
                    marshaler_type,
                    is_array() ? "Array" : "",
                    get_param_local(w));
            }
            else
            {
                w.write("%.DisposeMarshaler%(%);\n",
                    marshaler_type,
                    is_array() ? "Array" : "",
                    get_marshaler_local(w));
            }
        }
    };

    void set_abi_marshaler(writer& w, TypeSig const& type_sig, abi_marshaler& m, std::string_view prop_name = "")
    {
        auto semantics = get_type_semantics(type_sig);
        m.param_type = w.write_temp("%", bind<write_projection_type>(semantics));
        m.is_value_type = is_value_type(semantics);

        auto get_abi_type = [&]()
        {
            auto abi_type = w.write_temp("%", bind<write_type_name>(semantics, true, false));
            if (abi_type != prop_name)
            {
                return abi_type;
            }
            return w.write_temp("%", bind<write_type_name>(semantics, true, true));
        };

        auto set_simple_marshaler_type = [&](abi_marshaler& m, TypeDef const& type)
        {
            if (m.is_array())
            {
                m.marshaler_type = is_type_blittable(semantics) ? "MarshalBlittable" : "MarshalNonBlittable";
                m.marshaler_type += "<" + m.param_type + ">";
                m.local_type = m.marshaler_type + ".MarshalerArray";
            }
            else if (!is_type_blittable(type))
            {
                m.marshaler_type = get_abi_type();
                m.local_type = m.marshaler_type;
                if (!m.is_out()) m.local_type += ".Marshaler";
            }
        };

        auto set_typedef_marshaler = [&](abi_marshaler& m, TypeDef const& type)
        {
            switch (get_category(type))
            {
            case category::enum_type:
                break;
            case category::struct_type:
                set_simple_marshaler_type(m, type);
                break;
            case category::interface_type:
                if (get_mapped_type(type.TypeNamespace(), type.TypeName()) &&
                    type.TypeNamespace() == "Windows.Foundation.Collections")
                {
                    m.marshaler_type = get_abi_type();
                }
                else
                {
                    m.marshaler_type = "MarshalInterface<" + m.param_type + ">";
                }
                if (m.is_array())
                {
                    m.local_type = w.write_temp("MarshalInterfaceHelper<%>.MarshalerArray", m.param_type);
                }
                else
                {
                    m.local_type = m.is_out() ? "IntPtr" : "IObjectReference";
                }
                break;
            case category::class_type:
                m.marshaler_type = w.write_temp("%", bind<write_type_name>(semantics, true, true));
                if (m.is_array())
                {
                    m.local_type = w.write_temp("MarshalInterfaceHelper<%>.MarshalerArray", m.param_type);
                }
                else
                {
                    m.local_type = m.is_out() ? "IntPtr" : "IObjectReference";
                }
                break;
            case category::delegate_type:
                m.marshaler_type = get_abi_type();
                if (m.is_array())
                {
                    m.local_type = w.write_temp("MarshalInterfaceHelper<%>.MarshalerArray", m.param_type);
                }
                else
                {
                    m.local_type = m.is_out() ? "IntPtr" : "IObjectReference";
                }
                break;
            }
        };

        call(semantics,
            [&](object_type)
            {
                m.marshaler_type = "MarshalInspectable";
                if (m.is_array())
                {
                    m.local_type = "MarshalInterfaceHelper<object>.MarshalerArray";
                }
                else
                {
                    m.local_type = m.is_out() ? "IntPtr" : "IObjectReference";
                }
            },
            [&](type_definition const& type)
            {
                set_typedef_marshaler(m, type);
            },
            [&](generic_type_index const& /*var*/)
            {
                m.param_type = w.write_temp("%", bind<write_projection_type>(semantics));
                m.marshaler_type = w.write_temp("Marshaler<%>", m.param_type);
                m.local_type = "object";
            },
            [&](generic_type_instance const& type)
            {
                auto guard{ w.push_generic_args(type) };
                set_typedef_marshaler(m, type.generic_type);
            },
            [&](fundamental_type type)
            {
                if (type == fundamental_type::String)
                {
                    if (m.is_array())
                    {
                        m.marshaler_type = "MarshalString";
                        m.local_type = "MarshalString.MarshalerArray";
                    }
                    else
                    {
                        m.marshaler_type = "MarshalString";
                        m.local_type = m.is_out() ? "IntPtr" : "MarshalString";
                    }
                }
            },
            [&](auto const&) {});

        if (m.is_out() && m.local_type.empty())
        {
            m.local_type = w.write_temp("%", bind<write_abi_type>(semantics));
        }

        if (m.is_array() && m.marshaler_type.empty())
        {
            if (m.is_generic())
            {
                m.marshaler_type = w.write_temp("Marshaler<%>", m.param_type);
                m.local_type = "object";
            }
            else
            {
                m.marshaler_type = is_type_blittable(semantics) ? "MarshalBlittable" : "MarshalNonBlittable";
                m.marshaler_type += "<" + m.param_type + ">";
                m.local_type = m.marshaler_type + ".MarshalerArray";
            }
        }
    }

    auto get_abi_marshalers(writer& w, method_signature const& signature, bool is_generic, std::string_view prop_name = "", bool raw_return_type = false)
    {
        std::vector<abi_marshaler> marshalers;
        int param_index = 1;

        for (auto&& param : signature.params())
        {
            abi_marshaler m{
                std::string(param.first.Name()),
                is_generic ? param_index : -1,
                get_param_category(param)
            };
            param_index += m.is_array() ? 2 : 1;
            set_abi_marshaler(w, param.second->Type(), m, prop_name);
            marshalers.push_back(std::move(m));
        }

        if (auto ret = signature.return_signature())
        {
            abi_marshaler m{
                "retval",
                is_generic ? param_index : -1,
                ret.Type().is_szarray() && !raw_return_type ? param_category::receive_array : param_category::out,
                true
            };
            param_index += m.is_array() ? 2 : 1;
            if (!raw_return_type)
            {
                set_abi_marshaler(w, ret.Type(), m, prop_name);
            }
            else
            {
                m.param_type = w.write_temp("%", bind<write_abi_type>(get_type_semantics(ret.Type())));
                m.local_type = m.param_type;
                m.is_value_type = true;
            }
            marshalers.push_back(std::move(m));
        }

        return marshalers;
    }

    void write_abi_method_call_marshalers(writer& w, std::string_view invoke_target, bool is_generic, std::vector<abi_marshaler> const& marshalers)
    {
        auto write_abi_invoke = [&](writer& w)
        {
            if (is_generic)
            {
                w.write("%.DynamicInvokeAbi(__params);\n", invoke_target);
            }
            else
            {
                w.write("global::WinRT.ExceptionHelpers.ThrowExceptionForHR(%(ThisPtr%));\n",
                    invoke_target,
                    bind_each([](writer& w, abi_marshaler const& m)
                    {
                        w.write(", ");
                        m.write_marshal_to_abi(w);
                    }, marshalers));
            }
            for (auto&& m : marshalers)
            {
                m.write_marshal_from_abi(w);
            }
        };

        w.write("\n");
        for (auto&& m : marshalers)
        {
            m.write_locals(w);
        }
        if (is_generic)
        {
            w.write("var __params = new object[]{ ThisPtr");
            for (auto&& m : marshalers)
            {
                w.write(", ");
                if (m.is_array()) w.write("null, null");
                else if (!m.is_out() && m.marshaler_type.empty()) m.write_marshal_to_abi(w);
                else w.write("null");
            }
            w.write(" };\n");
        }

        bool have_disposers = std::find_if(marshalers.begin(), marshalers.end(), [](abi_marshaler const& m)
        {
            return !m.marshaler_type.empty();
        }) != marshalers.end();

        if (!have_disposers)
        {
            write_abi_invoke(w);
            return;
        }

        w.write(R"(try
{
%%}
finally
{
%}
)",
            bind_each([](writer& w, abi_marshaler const& m)
            {
                m.write_assignments(w);
            }, marshalers),
            bind(write_abi_invoke),
            bind_each([](writer& w, abi_marshaler const& m)
            {
                m.write_dispose(w);
            }, marshalers)
        );
    }

    void write_abi_method_call(writer& w, method_signature signature, std::string_view invoke_target, bool is_generic, bool raw_return_type = false)
    {
        write_abi_method_call_marshalers(w, invoke_target, is_generic, get_abi_marshalers(w, signature, is_generic, "", raw_return_type));
    }

    void write_abi_method_with_raw_return_type(writer& w, MethodDef const& method)
    {                if (is_special(method))
        {
            return;
        }

        auto get_method_info = [&](MethodDef const& method)
        {
            auto vmethod_name = get_vmethod_name(w, method.Parent(), method);
            method_signature signature{ method };
            return std::pair{
                "_obj.Vftbl." + vmethod_name,
                abi_signature_has_generic_parameters(w, signature)
            };
        };

        auto write_raw_return_type = [](writer& w, method_signature const& sig)
        {
            if (auto return_sig = sig.return_signature())
            {
                write_abi_type(w, get_type_semantics(return_sig.Type()));
            }
            else
            {
                w.write("void");
            }
        };

        method_signature signature{ method };
        auto [invoke_target, is_generic] = get_method_info(method);
        w.write(R"(
public unsafe new % %(%)
{%}
)",
            bind(write_raw_return_type, signature),
            method.Name(),
            bind_list<write_projection_parameter>(", ", signature.params()),
            bind<write_abi_method_call>(signature, invoke_target, is_generic, true));
    }


    void write_composing_factory_method(writer& w, MethodDef const& method)
    {
        if (is_special(method))
        {
            return;
        }

        auto get_method_info = [&](MethodDef const& method)
        {
            auto vmethod_name = get_vmethod_name(w, method.Parent(), method);
            method_signature signature{ method };
            return std::pair{
                "_obj.Vftbl." + vmethod_name,
                abi_signature_has_generic_parameters(w, signature)
            };
        };

        auto write_composable_constructor_params = [&](writer& w, method_signature const& method_sig)
        {
            auto const& params = method_sig.params();
            // We need to special case the last parameter
            separator s{ w };
            for (size_t i = 0; i < params.size() - 1; i++)
            {
                s();
                write_projection_parameter(w, params[i]);
            }

            // The innerIterface parameter is always an out IntPtr.
            XLANG_ASSERT(get_param_category(params[params.size() - 1]) == param_category::out);

            s();
            w.write("out IntPtr %",
                bind<write_parameter_name>(params[params.size() - 1]));
        };

        auto write_raw_return_type = [](writer& w, method_signature const& sig)
        {
            if (auto return_sig = sig.return_signature())
            {
                write_abi_type(w, get_type_semantics(return_sig.Type()));
            }
            else
            {
                w.write("void");
            }
        };

        method_signature signature{ method };
        auto [invoke_target, is_generic] = get_method_info(method);

        auto abi_marshalers = get_abi_marshalers(w, signature, is_generic, "", true);
        // The last abi marshaler is the return value and the second-to-last one
        // is the inner object (which is the return value we want).
        size_t inner_inspectable_index = abi_marshalers.size() - 2;
        abi_marshaler const& inner_inspectable_ref = abi_marshalers[inner_inspectable_index];
        abi_marshalers[inner_inspectable_index] = {
            inner_inspectable_ref.param_name,
            inner_inspectable_ref.param_index,
            inner_inspectable_ref.category,
            inner_inspectable_ref.is_return,
            "IntPtr",
            "IntPtr",
            {},
            true
        };

        w.write(R"(
public unsafe % %(%)
{%}
)",
            bind(write_raw_return_type, signature),
            method.Name(),
            bind(write_composable_constructor_params, signature),
            bind<write_abi_method_call_marshalers>(invoke_target, is_generic, abi_marshalers));
    }
    
    template<auto method_writer>
    std::string write_factory_cache_object(writer& w, TypeDef const& factory_type, TypeDef const& class_type)
    {
        std::string_view cache_type_name = factory_type.TypeName();

        auto cache_interface =
            w.write_temp(
                R"(ActivationFactory<%>.As<ABI.%.%.Vftbl>)",
                class_type.TypeName(),
                class_type.TypeNamespace(),
                cache_type_name);

        w.write(R"(
internal class _% : ABI.%.%
{
public _%() : base(%()) { }
private static WeakLazy<_%> _instance = new WeakLazy<_%>();
internal static _% Instance => _instance.Value;
%
}
)",
            cache_type_name,
            class_type.TypeNamespace(),
            cache_type_name,
            cache_type_name,
            cache_interface,
            cache_type_name,
            cache_type_name,
            cache_type_name,
            bind_each<method_writer>(factory_type.MethodList())
            );

        return w.write_temp("_%.Instance", cache_type_name);
    }


    void write_interface_members(writer& w, TypeDef const& type, std::set<std::string> const& generic_methods)
    {
        auto get_method_info = [&](MethodDef const& method)
        {
            auto vmethod_name = get_vmethod_name(w, type, method);
            return std::pair{
                "_obj.Vftbl." + vmethod_name,
                generic_methods.find(vmethod_name) != generic_methods.end()};
        };

        for (auto&& method : type.MethodList())
        {
            if (is_special(method))
            {
                continue;
            }
            method_signature signature{ method };
            auto [invoke_target, is_generic] = get_method_info(method);
            w.write(R"(
public unsafe %% %(%)
{%}
)",
                (method.Name() == "ToString"sv) ? "override " : "",
                bind<write_projection_return_type>(signature),
                method.Name(),
                bind_list<write_projection_parameter>(", ", signature.params()),
                bind<write_abi_method_call>(signature, invoke_target, is_generic, false));
        }

        for (auto&& prop : type.PropertyList())
        {
            auto [getter, setter] = get_property_methods(prop);
            w.write(R"(
public unsafe % %
{
)",
                write_prop_type(w, prop),
                prop.Name());
            if (getter)
            {
                auto [invoke_target, is_generic] = get_method_info(getter);
                auto signature = method_signature(getter);
                auto marshalers = get_abi_marshalers(w, signature, is_generic, prop.Name());
                w.write(R"(get
{%}
)",
                    bind<write_abi_method_call_marshalers>(invoke_target, is_generic, marshalers));
            }
            if (setter)
            {
                if (!getter)
                {
                    auto getter_interface = find_property_interface(w, type, prop.Name());
                    w.write("get{ return As<%>().%; }\n", getter_interface.first, prop.Name());
                }
                auto [invoke_target, is_generic] = get_method_info(setter);
                auto signature = method_signature(setter);
                auto marshalers = get_abi_marshalers(w, signature, is_generic, prop.Name());
                marshalers[0].param_name = "value";
                w.write(R"(set
{%}
)",
                    bind<write_abi_method_call_marshalers>(invoke_target, is_generic, marshalers));
            }
            w.write("}\n");
        }

        for (auto&& evt : type.EventList())
        {
            auto semantics = get_type_semantics(evt.EventType());
            w.write(R"(
public event % %
{
add => _%.Subscribe(value);
remove => _%.Unsubscribe(value);
}
)",
                bind<write_type_name>(get_type_semantics(evt.EventType()), false, false),
                evt.Name(),
                evt.Name(),
                evt.Name());
        }
    }

    struct required_interface
    {
        std::string members;
        std::string helper_wrapper;
        std::string adapter;
    };

    void write_required_interface_members_for_abi_type(writer& w, TypeDef const& type, 
        std::map<std::string, required_interface>& required_interfaces)
    {
        auto write_required_interface = [&](TypeDef const& iface)
        {
            auto interface_name = write_type_name_temp(w, iface);
            if (required_interfaces.find(interface_name) != required_interfaces.end())
            {
                // We've already written this required interface, so don't write it again.
                return;
            }

            if (auto mapping = get_mapped_type(iface.TypeNamespace(), iface.TypeName()))
            {
                auto remove_enumerable = [&](std::string generic_enumerable = "")
                {
                    required_interfaces[std::move("global::System.Collections.IEnumerable")] = {};
                    if(generic_enumerable.empty()) return;
                    required_interfaces[std::move(generic_enumerable)] = {};
                };

                if (mapping->abi_name == "IIterable`1") // IEnumerable`1
                {
                    auto element = w.write_temp("%", bind<write_generic_type_name>(0));
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_enumerable_members>("_iterableToEnumerable", true)),
                        w.write_temp("ABI.System.Collections.Generic.IEnumerable<%>", element),
                        "_iterableToEnumerable"
                    };
                    remove_enumerable();
                }
                else if (mapping->abi_name == "IIterator`1") // IEnumerator`1
                {
                    auto element = w.write_temp("%", bind<write_generic_type_name>(0));
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_enumerator_members>("_iteratorToEnumerator")),
                        w.write_temp("ABI.System.Collections.Generic.IEnumerator<%>", element),
                        "_iteratorToEnumerator"
                    };
                }
                else if (mapping->abi_name == "IMapView`2") // IReadOnlyDictionary`2
                {
                    auto key = w.write_temp("%", bind<write_generic_type_name>(0));
                    auto value = w.write_temp("%", bind<write_generic_type_name>(1));
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_readonlydictionary_members>("_mapViewToReadOnlyDictionary", true)),
                        w.write_temp("ABI.System.Collections.Generic.IReadOnlyDictionary<%, %>", key, value),
                        "_mapViewToReadOnlyDictionary"
                    };
                    remove_enumerable(w.write_temp("global::System.Collections.Generic.IEnumerable<global::System.Collections.Generic.KeyValuePair<%, %>>", key, value));
                }
                else if (mapping->abi_name == "IMap`2") // IDictionary<TKey, TValue> 
                {
                    auto key = w.write_temp("%", bind<write_generic_type_name>(0));
                    auto value = w.write_temp("%", bind<write_generic_type_name>(1));
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_dictionary_members>("_mapToDictionary", true)),
                        w.write_temp("ABI.System.Collections.Generic.IDictionary<%, %>", key, value),
                        "_mapToDictionary"
                    };
                    remove_enumerable(w.write_temp("global::System.Collections.Generic.IEnumerable<global::System.Collections.Generic.KeyValuePair<%, %>>", key, value));
                }
                else if (mapping->abi_name == "IVectorView`1") // IReadOnlyList`1
                {
                    auto element = w.write_temp("%", bind<write_generic_type_name>(0));
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_readonlylist_members>("_vectorViewToReadOnlyList", true)),
                        w.write_temp("ABI.System.Collections.Generic.IReadOnlyList<%>", element),
                        "_vectorViewToReadOnlyList"
                    };
                    remove_enumerable(w.write_temp("global::System.Collections.Generic.IEnumerable<%>", element));
                }
                else if (mapping->abi_name == "IVector`1") // IList`1
                {
                    auto element = w.write_temp("%", bind<write_generic_type_name>(0));
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_list_members>("_vectorToList", true)),
                        w.write_temp("ABI.System.Collections.Generic.IList<%>", element),
                        "_vectorToList"
                    };
                    remove_enumerable(w.write_temp("global::System.Collections.Generic.IEnumerable<%>", element));
                }
                else if (mapping->abi_name == "IBindableIterable") // IEnumerable
                {
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_nongeneric_enumerable_members>("_bindableIterableToEnumerable")),
                        "ABI.System.Collections.IEnumerable",
                        "_bindableIterableToEnumerable"
                    };
                }
                else if (mapping->abi_name == "IBindableVector") // IList
                {
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_nongeneric_list_members>("_bindableVectorToList", true)),
                        "ABI.System.Collections.IList",
                        "_bindableVectorToList"
                    };
                    remove_enumerable();
                }
                else if (mapping->mapped_name == "IDisposable")
                {
                    required_interfaces[std::move(interface_name)] =
                    {
                        w.write_temp("%", bind<write_idisposable_members>("As<global::ABI.System.IDisposable>()"))
                    };
                }
                return;
            }

            auto methods = w.write_temp("%",
            [&](writer& w)
            {
                for (auto&& method : iface.MethodList())
                {
                    if (!method.SpecialName())
                    {
                        auto method_target = w.write_temp("As<%>()", bind<write_type_name>(iface, true, false));
                        auto return_type = w.write_temp("%", bind<write_projection_return_type>(method_signature{ method }));
                        write_explicitly_implemented_method(w, method, return_type, iface, method_target);
                    }
                }
                w.write_each<write_explicitly_implemented_property>(iface.PropertyList(), iface, true);
                w.write_each<write_explicitly_implemented_event>(iface.EventList(), iface, true);
            });
            required_interfaces[std::move(interface_name)] = { methods };
        };
        
        for (auto&& iface : type.InterfaceImpl())
        {
            for_typedef(w, get_type_semantics(iface.Interface()), [&](auto type)
            {
                if (has_attribute(iface, "Windows.Foundation.Metadata", "OverridableAttribute") || !is_exclusive_to(type))
                {
                    write_required_interface(type);
                    write_required_interface_members_for_abi_type(w, type, required_interfaces);
                }
            });
        }
    }

    void write_guid_attribute(writer& w, TypeDef const& type)
    {
        auto fully_qualify_guid = (type.TypeNamespace() == "Windows.Foundation.Metadata");

        auto attribute = get_attribute(type, "Windows.Foundation.Metadata", "GuidAttribute");
        if (!attribute)
        {
            throw_invalid("'Windows.Foundation.Metadata.GuidAttribute' attribute for type '", type.TypeNamespace(), ".", type.TypeName(), "' not found");
        }

        auto args = attribute.Value().FixedArgs();

        using std::get;

        auto get_arg = [&](decltype(args)::size_type index) { return get<ElemSig>(args[index].value).value; };

        w.write_printf(R"([%s("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X")])",
            fully_qualify_guid ? "global::System.Runtime.InteropServices.Guid" : "Guid",
            get<uint32_t>(get_arg(0)),
            get<uint16_t>(get_arg(1)),
            get<uint16_t>(get_arg(2)),
            get<uint8_t>(get_arg(3)),
            get<uint8_t>(get_arg(4)),
            get<uint8_t>(get_arg(5)),
            get<uint8_t>(get_arg(6)),
            get<uint8_t>(get_arg(7)),
            get<uint8_t>(get_arg(8)),
            get<uint8_t>(get_arg(9)),
            get<uint8_t>(get_arg(10)));
    }

    void write_type_inheritance(writer& w, TypeDef const& type, type_semantics base_semantics, bool add_custom_qi)
    {
        auto delimiter{ " : " };
        auto write_delimiter = [&]()
        {
            w.write(delimiter);
            delimiter = ", ";
        };

        if (!std::holds_alternative<object_type>(base_semantics))
        {
            write_delimiter();
            write_projection_type(w, base_semantics);
        }

        for (auto&& iface : type.InterfaceImpl())
        {
            for_typedef(w, get_type_semantics(iface.Interface()), [&](auto type)
            {
                if (has_attribute(iface, "Windows.Foundation.Metadata", "OverridableAttribute") || !is_exclusive_to(type))
                {
                    write_delimiter();
                    w.write("%", bind<write_type_name>(type, false, false));
                }
            });
        }

        if (add_custom_qi)
        {
            write_delimiter();
            w.write("global::System.Runtime.InteropServices.ICustomQueryInterface");
        }
    }
    
    std::string get_vmethod_delegate_type(writer& w, MethodDef const& method, std::string vmethod_name)
    {
        method_signature signature{ method };
        if (is_special(method))
        {
            bool getter = starts_with(method.Name(), "get_");
            bool setter = starts_with(method.Name(), "put_");
            if (getter || setter)
            {
                std::string suffix{};
                auto prop_type = getter ? signature.return_signature().Type() : signature.params()[0].second->Type();
                if (prop_type.is_szarray())
                {
                    return "";
                }
                call(get_type_semantics(prop_type),
                    [&](guid_type) { suffix = "Guid"; },
                    [&](type_type) { throw_invalid("System.Type not implemented"); },
                    [&](fundamental_type const& type) { suffix = get_delegate_type_suffix(type); },
                    [&](generic_type_index const& /*var*/) {},
                    [&](type_definition const& /*type*/) {},
                    [&](auto) { suffix = "Object"; });
                if (!suffix.empty())
                {
                    return w.write_temp("%_PropertyAs%", (getter ? "_get" : "_put"), suffix);
                }
            }
            else if (starts_with(method.Name(), "add_"))
            {
                return "_add_EventHandler";
            }
            else if (starts_with(method.Name(), "remove_"))
            {
                return "_remove_EventHandler";
            }
        }
        return "";
    }


    struct generic_abi_param
    {
        std::string abi_type;
        std::string generic_param;
        std::string param_name;
    };

    std::pair<std::string, std::string> get_generic_abi_type(writer& w, type_semantics semantics)
    {
        std::string generic_param{};
        writer::write_generic_type_name_guard g(w, [&](writer& w, uint32_t index)
        {
            auto generic_type_name = w.write_temp("%", bind<write_generic_type_name_base>(index));
            generic_param = generic_type_name + "Abi";
            w.write("Marshaler<%>.AbiType", generic_type_name);
        });
        auto generic_abi_type = w.write_temp("%", bind<write_abi_type>(semantics));
        return {generic_abi_type, generic_param};
    }

    auto get_generic_abi_types(writer& w, method_signature const& signature)
    {
        std::vector<std::pair<std::string, std::string>> generic_abi_types;
        auto add_generic_abi_type = [&](TypeSig sig, bool byref)
        {
            auto const [generic_abi_type, generic_param] = get_generic_abi_type(w, get_type_semantics(sig));
            generic_abi_types.push_back({w.write_temp(!generic_param.empty() ? "%%" : "typeof(%)%",
                generic_abi_type, byref ? ".MakeByRefType()" : ""), generic_param });
        };

        auto add_array_param = [&](param_category category)
        {
            XLANG_ASSERT(category > param_category::out);
            switch (category)
            {
            case param_category::pass_array:
                generic_abi_types.push_back({ "typeof(int)", "" });
                generic_abi_types.push_back({ "typeof(IntPtr)", "" });
                break;
            case param_category::fill_array:
                generic_abi_types.push_back({ "typeof(int)", "" });
                generic_abi_types.push_back({ "typeof(IntPtr).MakeByRefType()", "" });
                break;
            case param_category::receive_array:
                generic_abi_types.push_back({ "typeof(int).MakeByRefType()", "" });
                generic_abi_types.push_back({ "typeof(IntPtr).MakeByRefType()", "" });
                break;
            }
        };

        for (auto&& param : signature.params())
        {
            param_category category = get_param_category(param);
            if (category <= param_category::out)
            {
                add_generic_abi_type(param.second->Type(), category == param_category::out);
            }
            else
            {
                add_array_param(category);
            }
        }
        if (signature.return_signature())
        {
            if (!signature.return_signature().Type().is_szarray())
            {
                add_generic_abi_type(signature.return_signature().Type(), true);
            }
            else
            {
                add_array_param(param_category::receive_array);
            }
        }
        return generic_abi_types;
    }

    void write_abi_signature(writer& w, MethodDef const& method)
    {
        bool is_generic = distance(method.GenericParam()) > 0;
        method_signature signature{ method };
        auto generic_abi_types = get_generic_abi_types(w, signature);
        bool have_generic_params = std::find_if(generic_abi_types.begin(), generic_abi_types.end(),
            [](auto&& pair) { return !pair.second.empty(); }) != generic_abi_types.end();

        if (!is_generic && !have_generic_params)
        {
            w.write("(%)", bind<write_abi_parameters>(signature));
            return;
        }
        if (have_generic_params)
        {
            w.write("<");
            int count = 0;
            for (auto&& pair : generic_abi_types)
            {
                if (pair.second.empty()) continue;
                w.write(count++ == 0 ? "" : ", ");
                w.write(pair.second);
            }
            w.write(">");
        }
        w.write(have_generic_params ? "(void* thisPtr" : "(IntPtr thisPtr");
        int index = 0;
        for (auto&& param : signature.params())
        {
            auto generic_type = generic_abi_types[index++].second;
            auto param_cat = get_param_category(param);
            if (!generic_type.empty() && (param_cat <= param_category::out))
            {
                w.write(", %% %",
                    param_cat == param_category::out ? "out " : "",
                    generic_type,
                    bind<write_parameter_name>(param));
            }
            else
            {
                write_abi_parameter(w, param);
            }
        }
        if (auto return_sig = signature.return_signature())
        {
            auto generic_type = generic_abi_types[index++].second;
            if (!return_sig.Type().is_szarray() && !generic_type.empty())
            {
                w.write(", out % %", generic_type, 
                    bind<write_escaped_identifier>(signature.return_param_name()));
            }
            else
            {
                write_abi_return(w, signature);
            }
        }
        w.write(")");
    }

    struct managed_marshaler
    {
        std::string param_name;
        int param_index;
        param_category category;
        std::string param_type;
        std::string local_type;
        std::string marshaler_type;
        bool abi_boxed;

        bool is_out() const
        {
            return (category == param_category::out) ||
                (category == param_category::receive_array);
        }

        bool is_ref() const
        {
            return (category == param_category::fill_array);
        }

        bool is_generic() const
        {
            return param_index > -1;
        }

        bool is_array() const
        {
            return category >= param_category::pass_array;
        }

        std::string get_param_local(writer& w) const
        {
            return is_generic() ?
                w.write_temp("__params[%]", param_index) :
                w.write_temp("__%", param_name);
        }

        void write_local(writer& w) const
        {
            XLANG_ASSERT(!is_generic());
            if ((category == param_category::in) || (category == param_category::pass_array))
                return;
            if (category == param_category::fill_array)
            {
                w.write("% __% = %.FromAbiArray((__%Size, %));\n",
                    local_type,
                    param_name,
                    marshaler_type,
                    param_name, bind<write_escaped_identifier>(param_name));
                return;
            }
            std::string_view out_local_type;
            if (param_type == "bool")
            {
                out_local_type = is_array() ? "bool[]" : "bool";
            }
            else if (param_type == "char")
            {
                out_local_type = is_array() ? "char[]" : "char";
            }
            else
            {
                out_local_type = local_type;
            }
            w.write("% __% = default;\n",
                out_local_type,
                param_name);
        }

        void write_out_initialize(writer& w) const
        {
            XLANG_ASSERT(is_out());
            w.write("% = default;\n", bind<write_escaped_identifier>(param_name));
            if (is_array())
            {
                w.write("__%Size = default;\n", param_name);
            }
        }

        void write_marshal_to_managed(writer& w) const
        {
            if (is_out() || is_ref())
            {
                is_generic() ?
                    w.write("null") :
                    w.write("% __%", is_out() ? "out" : "", param_name);
            }
            else if (marshaler_type.empty())
            {
                std::string_view format_string;
                if (param_type == "bool")
                {
                    format_string = is_generic() ? "(byte)% != 0" : "% != 0";
                } 
                else if (param_type == "char")
                {
                    format_string = is_generic() ? "(char)(ushort)%" : "(char)%";
                }
                else
                {
                    format_string = "%";
                }
                w.write(format_string, bind<write_escaped_identifier>(param_name));
            }
            else if (is_array())
            {
                w.write("%.FromAbiArray((__%Size, %))",
                    marshaler_type,
                    param_name, bind<write_escaped_identifier>(param_name));
            }
            else
            {
                w.write("%.FromAbi(%)",
                    marshaler_type,
                    bind<write_escaped_identifier>(param_name));
            }
        }

        void write_marshal_from_managed(writer& w) const
        {
            if (!is_ref() && (!is_out() || local_type.empty()))
                return;
            auto param_local = get_param_local(w);
            if (is_ref())
            {
                w.write("%.CopyManagedArray(%, %);\n",
                    marshaler_type,
                    param_local,
                    bind<write_escaped_identifier>(param_name));
                return;
            }
            is_array() ?
                w.write("(__%Size, %) = ", param_name, bind<write_escaped_identifier>(param_name)) :
                w.write("% = ", bind<write_escaped_identifier>(param_name));
            auto param_cast = is_generic() ? w.write_temp("(%)", param_type) : "";
            if (marshaler_type.empty())
            {
                if (local_type == "IntPtr")
                {
                    w.write("%.FromManaged(%);",
                        param_type,
                        param_local);
                }
                else
                {
                    if (param_type == "bool")
                    {
                        w.write("(byte)(% ? 1 : 0);", param_local);
                    }
                    else if (param_type == "char")
                    {
                        w.write("(ushort)%;", param_local);
                    }
                    else
                    {
                        w.write("%%;", param_cast, param_local);
                    }
                }
            }
            else
            {
                w.write("%%.FromManaged%(%);",
                    abi_boxed && !is_array() ?
                        w.write_temp("(%)", param_type) : "",
                    marshaler_type,
                    is_array() ? "Array" : "",
                    param_local);
            }
            w.write("\n");
        }
    };

    auto get_managed_marshalers(writer& w, method_signature const& signature, bool is_generic)
    {
        std::vector<managed_marshaler> marshalers;

        auto set_marshaler = [](writer& w, type_semantics const& semantics, managed_marshaler& m)
        {
            m.param_type = w.write_temp("%", bind<write_projection_type>(semantics));

            auto get_abi_type = [&]()
            {
                return w.write_temp("%", bind<write_type_name>(semantics, true, true));
            };

            auto set_typedef_marshaler = [&](TypeDef const& type)
            {
                switch (get_category(type))
                {
                case category::enum_type:
                    break;
                case category::struct_type:
                    if (!is_type_blittable(type))
                    {
                        if (!m.is_array())
                        {
                            m.marshaler_type = get_abi_type();
                        }
                        m.local_type = m.param_type;
                    }
                    break;
                case category::interface_type:
                    if (get_mapped_type(type.TypeNamespace(), type.TypeName()) &&
                        type.TypeNamespace() == "Windows.Foundation.Collections")
                    {
                        m.marshaler_type = get_abi_type();
                    }
                    else
                    {
                        m.marshaler_type = w.write_temp("MarshalInterface<%>", m.param_type);
                    }
                    m.local_type = m.param_type;
                    break;
                case category::class_type:
                    m.marshaler_type = get_abi_type();
                    m.local_type = m.param_type;
                    break;
                case category::delegate_type:
                    m.marshaler_type = get_abi_type();
                    m.local_type = m.param_type;
                    break;
                }
            };

            call(semantics,
                [&](object_type const&)
                {
                    m.marshaler_type = "MarshalInspectable";
                    m.local_type = "object";
                },
                [&](type_definition const& type)
                {
                    set_typedef_marshaler(type);
                },
                [&](generic_type_index const& /*var*/)
                {
                    m.param_type = get_generic_abi_type(w, semantics).second;
                    m.local_type = w.write_temp("%", bind<write_projection_type>(semantics));
                    m.marshaler_type = w.write_temp("Marshaler<%>", m.local_type);
                    m.abi_boxed = true;
                },
                [&](generic_type_instance const& type)
                {
                    auto guard{ w.push_generic_args(type) };
                    set_typedef_marshaler(type.generic_type);
                },
                [&](fundamental_type type)
                {
                    if (type == fundamental_type::String)
                    {
                        m.marshaler_type = "MarshalString";
                        m.local_type = m.is_out() ? "string" : "";
                    }
                },
                [&](auto const&) {});

            if (m.is_out() && m.local_type.empty())
            {
                m.local_type = w.write_temp("%", bind<write_abi_type>(semantics));
            }
            if (m.is_array())
            {
                if (m.marshaler_type.empty())
                {
                    m.marshaler_type = is_type_blittable(semantics) ? "MarshalBlittable" : "MarshalNonBlittable";
                    m.marshaler_type += "<" + m.param_type + ">";
                }
                m.local_type = (m.local_type.empty() ? m.param_type : m.local_type) + "[]";
            }
        };

        for (auto&& param : signature.params())
        {
            managed_marshaler m{
                std::string(param.first.Name()),
                is_generic ? (int)marshalers.size() : -1
            };
            m.category = get_param_category(param);
            set_marshaler(w, get_type_semantics(param.second->Type()), m);
            marshalers.push_back(std::move(m));
        }

        if (auto ret = signature.return_signature())
        {
            managed_marshaler m{
                std::string(signature.return_param_name()),
                -1,
                ret.Type().is_szarray() ? param_category::receive_array : param_category::out
            };
            set_marshaler(w, get_type_semantics(ret.Type()), m);
            return std::pair{ marshalers, m };
        }

        return std::pair{ marshalers, managed_marshaler{} };
    }

    void write_managed_method_call(writer& w, method_signature signature, std::string invoke_expression_format)
    {
        auto managed_marshalers = get_managed_marshalers(w, signature, false);
        auto marshalers = managed_marshalers.first;
        auto return_marshaler = managed_marshalers.second;
        auto return_sig = signature.return_signature();
        
        w.write(
R"(%
%
try
{
%
%%
}
catch (Exception __exception__)
{
global::WinRT.ExceptionHelpers.SetErrorInfo(__exception__);
return global::WinRT.ExceptionHelpers.GetHRForException(__exception__);
}
return 0;)",
            [&](writer& w) {
                if (!return_sig) return;
                return_marshaler.write_local(w);
            },
            [&](writer& w) {
                w.write(bind_each([](writer& w, managed_marshaler const& m)
                {
                    if (m.is_out())
                    {
                        m.write_out_initialize(w);
                    }
                }, marshalers));
                if (return_sig)
                {
                    return_marshaler.write_out_initialize(w);
                }
                w.write(bind_each([](writer& w, managed_marshaler const& m)
                {
                    m.write_local(w);
                }, marshalers));
            },
            [&](writer& w)
            {
                if (return_sig)
                {
                    w.write("__% = ", return_marshaler.param_name);
                }

                w.write(R"(%;)",
                    bind([&](writer& w)
                    {
                        w.write(invoke_expression_format,
                            bind_list([](writer& w, managed_marshaler const& m)
                                {
                                    m.write_marshal_to_managed(w);
                                }, ", ", marshalers));
                    }));
            },
            bind_each([](writer& w, managed_marshaler const& m)
            { 
                m.write_marshal_from_managed(w);
            }, marshalers),
            [&](writer& w) {
                if (!return_sig) return;
                return_marshaler.write_marshal_from_managed(w);
            });
    }

    void write_method_abi_invoke(writer& w, MethodDef const& method)
    {
        if (method.SpecialName()) return;

        method_signature signature{ method };
        auto return_sig = signature.return_signature();
        auto type_name = write_type_name_temp(w, method.Parent());
        auto vmethod_name = get_vmethod_name(w, method.Parent(), method);

        auto generic_abi_types = get_generic_abi_types(w, signature);
        bool have_generic_params = std::find_if(generic_abi_types.begin(), generic_abi_types.end(),
            [](auto&& pair) { return !pair.second.empty(); }) != generic_abi_types.end();

        w.write(
            R"(
private static unsafe int Do_Abi_%%
{
%
})",
            vmethod_name,
            bind<write_abi_signature>(method),
            bind<write_managed_method_call>(
                signature,
                w.write_temp("global::WinRT.ComWrappersSupport.FindObject<%>(%).%%",
                    type_name,
                    have_generic_params ? "new IntPtr(thisPtr)" : "thisPtr",
                    method.Name(),
                    "(%)")));
    }

    void write_property_abi_invoke(writer& w, Property const& prop)
    {
        auto [getter, setter] = get_property_methods(prop);
        auto type_name = write_type_name_temp(w, prop.Parent());
        if (setter)
        {
            method_signature setter_sig{ setter };
            auto vmethod_name = get_vmethod_name(w, setter.Parent(), setter);

            auto generic_abi_types = get_generic_abi_types(w, setter_sig);
            bool have_generic_params = std::find_if(generic_abi_types.begin(), generic_abi_types.end(),
                [](auto&& pair) { return !pair.second.empty(); }) != generic_abi_types.end();

            // WinRT properties can't be indexers.
            XLANG_ASSERT(setter_sig.params().size() == 1);

        w.write(
            R"(
private static unsafe int Do_Abi_%%
{
%
})",
            vmethod_name,
            bind<write_abi_signature>(setter),
            bind<write_managed_method_call>(
                setter_sig,
                w.write_temp("global::WinRT.ComWrappersSupport.FindObject<%>(%).% = %",
                    type_name,
                    have_generic_params ? "new IntPtr(thisPtr)" : "thisPtr",
                    prop.Name(),
                    "%")));
        }

        if (getter)
        {
            method_signature getter_sig{ getter };
            auto vmethod_name = get_vmethod_name(w, getter.Parent(), getter);

            auto generic_abi_types = get_generic_abi_types(w, getter_sig);
            bool have_generic_params = std::find_if(generic_abi_types.begin(), generic_abi_types.end(),
                [](auto&& pair) { return !pair.second.empty(); }) != generic_abi_types.end();

            // WinRT properties can't be indexers.
            XLANG_ASSERT(getter_sig.params().size() == 0);
            w.write(
                R"(
private static unsafe int Do_Abi_%%
{
%
})",
                vmethod_name,
                bind<write_abi_signature>(getter),
                bind<write_managed_method_call>(
                    getter_sig,
                    w.write_temp("global::WinRT.ComWrappersSupport.FindObject<%>(%).%%",
                        type_name,
                        have_generic_params ? "new IntPtr(thisPtr)" : "thisPtr",
                        prop.Name(),
                        "%")));
        }

    }

    void write_event_abi_invoke(writer& w, Event const& evt)
    {
        auto type_name = write_type_name_temp(w, evt.Parent());
        auto semantics = get_type_semantics(evt.EventType());
        auto [add_method, remove_method] = get_event_methods(evt);
        auto add_signature = method_signature{ add_method };

        auto handler_parameter_name = add_signature.params().back().first.Name();
        auto add_handler_event_token_name = add_signature.return_param_name();
        auto remove_handler_event_token_name = method_signature{ remove_method }.params().back().first.Name();

        w.write("\nprivate static global::System.Runtime.CompilerServices.ConditionalWeakTable<%, global::WinRT.EventRegistrationTokenTable<%>> _%_TokenTables = new global::System.Runtime.CompilerServices.ConditionalWeakTable<%, global::WinRT.EventRegistrationTokenTable<%>>();",
            type_name,
            bind<write_type_name>(semantics, false, false),
            evt.Name(),
            type_name,
            bind<write_type_name>(semantics, false, false));

        w.write(
            R"(
private static unsafe int Do_Abi_%%
{
% = default;
try
{
var __this = global::WinRT.ComWrappersSupport.FindObject<%>(thisPtr);
var __handler = %.FromAbi(%);
% = _%_TokenTables.GetOrCreateValue(__this).AddEventHandler(__handler);
__this.% += __handler;
return 0;
}
catch (Exception __ex)
{
return __ex.HResult;
}
})",
            get_vmethod_name(w, add_method.Parent(), add_method),
            bind<write_abi_signature>(add_method),
            add_handler_event_token_name,
            type_name,
            bind<write_type_name>(semantics, true, false),
            handler_parameter_name,
            add_handler_event_token_name,
            evt.Name(),
            evt.Name());
        w.write(
    R"(
private static unsafe int Do_Abi_%%
{
try
{
var __this = global::WinRT.ComWrappersSupport.FindObject<%>(thisPtr);
if(_%_TokenTables.TryGetValue(__this, out var __table) && __table.RemoveEventHandler(%, out var __handler))
{
__this.% -= __handler;
}
return 0;
}
catch (Exception __ex)
{
return __ex.HResult;
}
})",
            get_vmethod_name(w, remove_method.Parent(), remove_method),
            bind<write_abi_signature>(remove_method),
            type_name,
            evt.Name(),
            remove_handler_event_token_name,
            evt.Name());
    }

    void write_vtable(writer& w, TypeDef const& type, std::string const& type_name,
        std::set<std::string>& generic_methods,
        std::string const& nongenerics_class,
        std::vector<std::string>& nongeneric_delegates)
    {
        auto methods = type.MethodList();
        auto is_generic = distance(type.GenericParam()) > 0;
        std::vector<std::string> method_marshals_to_abi;
        std::vector<std::string> method_marshals_to_projection;
        std::vector<std::string> method_create_delegates_to_projection;

        w.write(R"(%
public struct Vftbl
{
internal IInspectable.Vftbl IInspectableVftbl;
%%%%%%
})",
            bind<write_guid_attribute>(type),
            bind_each([&](writer& w, MethodDef const& method)
            {
                bool signature_has_generic_parameters{};

                auto generic_abi_types = get_generic_abi_types(w, method_signature{ method });
                bool have_generic_type_parameters = std::find_if(generic_abi_types.begin(), generic_abi_types.end(),
                    [](auto&& pair) { return !pair.second.empty(); }) != generic_abi_types.end();

                auto vmethod_name = get_vmethod_name(w, type, method);
                auto delegate_type = get_vmethod_delegate_type(w, method, vmethod_name);
                if(delegate_type == "")
                {
                    delegate_type = nongenerics_class + "." + vmethod_name;
                    writer::write_generic_type_name_guard g(w, [&](writer& /*w*/, uint32_t /*index*/) {
                        signature_has_generic_parameters = true;
                    });
                    auto delegate_definition = w.write_temp("public unsafe delegate int %(%);\n",
                        vmethod_name,
                        bind<write_abi_parameters>(method_signature{ method }));
                    if (signature_has_generic_parameters)
                    {
                        delegate_type = "global::System.Delegate";
                    }
                    else
                    {
                        nongeneric_delegates.push_back(delegate_definition);
                    }
                }
                w.write("public % %;\n", delegate_type, vmethod_name);
                uint32_t const vtable_index = method.index() - methods.first.index() + 6;
                if (is_generic)
                {
                    method_marshals_to_abi.emplace_back(signature_has_generic_parameters ?
                        w.write_temp("% = Marshal.GetDelegateForFunctionPointer(vftbl[%], %_Type);\n",
                            vmethod_name, vtable_index, vmethod_name) :
                        w.write_temp("% = Marshal.GetDelegateForFunctionPointer<%>(vftbl[%]);\n",
                            vmethod_name, delegate_type, vtable_index)
                        );
                    method_marshals_to_projection.emplace_back(
                        w.write_temp("nativeVftbl[%] = Marshal.GetFunctionPointerForDelegate(AbiToProjectionVftable.%);\n",
                            vtable_index, vmethod_name)
                        );

                    method_create_delegates_to_projection.emplace_back(have_generic_type_parameters ?
                        w.write_temp(R"(% = %global::System.Delegate.CreateDelegate(%, typeof(Vftbl).GetMethod("Do_Abi_%", BindingFlags.NonPublic | BindingFlags.Static).MakeGenericMethod(%)))",
                            vmethod_name,
                            !signature_has_generic_parameters ? w.write_temp("(%)", delegate_type) : "",
                            !signature_has_generic_parameters ? w.write_temp("typeof(%)", delegate_type) : vmethod_name + "_Type",
                            vmethod_name,
                            bind([&](writer& w, method_signature const& sig)
                                {
                                    separator s{ w };
                                    auto write_abi_type = [&](writer& w, type_semantics type)
                                    {
                                        auto const [generic_abi_type, generic_type_parameter] = get_generic_abi_type(w, type);
                                        if (!generic_type_parameter.empty())
                                        {
                                            s();
                                            w.write(generic_abi_type);
                                        }
                                    };
                                    for (auto&& param : sig.params())
                                    {
                                        write_abi_type(w, get_type_semantics(param.second->Type()));
                                    }
                                    if (sig.return_signature())
                                    {
                                        write_abi_type(w, get_type_semantics(sig.return_signature().Type()));
                                    }
                                }, method_signature{ method })) :
                    w.write_temp("% = Do_Abi_%",
                        vmethod_name, vmethod_name)
                        );
                }
                else
                {
                    method_create_delegates_to_projection.emplace_back(
                        w.write_temp("% = Do_Abi_%",
                            vmethod_name, vmethod_name)
                        );
                }
            }, methods),
            [&](writer& w)
            {
                if (!is_generic) return;
                w.write("public static Guid PIID = GuidGenerator.CreateIID(typeof(%));\n", type_name);
                w.write(R"(%
internal unsafe Vftbl(IntPtr thisPtr)
{
var vftblPtr = Marshal.PtrToStructure<VftblPtr>(thisPtr);
var vftbl = (IntPtr*)vftblPtr.Vftbl;
IInspectableVftbl = Marshal.PtrToStructure<IInspectable.Vftbl>(vftblPtr.Vftbl);
%}
)",
                    bind_each([&](writer& w, MethodDef const& method)
                    {
                        auto vmethod_name = get_vmethod_name(w, type, method);

                        if (abi_signature_has_generic_parameters(w, method_signature{ method }))
                        {
                            auto generic_abi_types = get_generic_abi_types(w, method_signature{ method });

                            w.write("public static readonly Type %_Type = Expression.GetDelegateType(new Type[]{ typeof(void*), %typeof(int) });\n",
                                vmethod_name,
                                bind_each([&](writer& w, auto&& pair)
                                {
                                    w.write("%, ", pair.first);
                                }, generic_abi_types));
                            generic_methods.insert(vmethod_name);
                        }
                    }, methods),
                    bind_each(method_marshals_to_abi)
                );
            },
            bind([&](writer& w)
            {
                w.write(R"(
private static readonly Vftbl AbiToProjectionVftable;
public static readonly IntPtr AbiToProjectionVftablePtr;
static unsafe Vftbl()
{
AbiToProjectionVftable = new Vftbl
{
IInspectableVftbl = global::WinRT.IInspectable.Vftbl.AbiToProjectionVftable, 
%
};
var nativeVftbl = (IntPtr*)ComWrappersSupport.AllocateVtableMemory(typeof(Vftbl), Marshal.SizeOf<global::WinRT.IInspectable.Vftbl>() + sizeof(IntPtr) * %);
%
AbiToProjectionVftablePtr = (IntPtr)nativeVftbl;
}
)",
                    bind_list(",\n", method_create_delegates_to_projection),
                    std::to_string(distance(methods)),
                    bind([&](writer& w)
                        {
                            if (!is_generic)
                            {
                                w.write("Marshal.StructureToPtr(AbiToProjectionVftable, (IntPtr)nativeVftbl, false);");
                            }
                            else
                            {
                                w.write("Marshal.StructureToPtr(AbiToProjectionVftable.IInspectableVftbl, (IntPtr)nativeVftbl, false);\n");
                                w.write("%", bind_each(method_marshals_to_projection));
                            }
                        }));
            }),
            bind_each<write_method_abi_invoke>(methods),
            bind_each<write_property_abi_invoke>(type.PropertyList()),
            bind_each<write_event_abi_invoke>(type.EventList())
        );
    }

    void write_base_constructor_dispatch(writer& w, type_semantics type)
    {
        std::string base_default_interface_name;
        call(type,
            [&](object_type) {},
            [&](type_definition const& def)
            {
                base_default_interface_name = get_default_interface_name(w, def);
            },
            [&](generic_type_instance const& inst)
            {
                auto guard{ w.push_generic_args(inst) };
                base_default_interface_name = get_default_interface_name(w, inst.generic_type);
            },
            [](auto)
            {
                throw_invalid("Invalid base class type.");
            });

        if (!std::holds_alternative<object_type>(type))
        {
            w.write(R"(
    : base(ifc.As<%>())
)",
                base_default_interface_name);
        }
    }

    void write_custom_attributes(writer& w, TypeDef const& type)
    {
        auto write_fixed_arg = [&](writer& w, FixedArgSig arg)
        {
            if (std::holds_alternative<std::vector<ElemSig>>(arg.value))
            {
                throw_invalid("ElemSig list unexpected");
            }
            auto&& arg_value = std::get<ElemSig>(arg.value);

            call(arg_value.value,
                [&](ElemSig::SystemType system_type)
                {
                    auto arg_type = type.get_cache().find_required(system_type.name);
                    w.write("typeof(%)", bind<write_projection_type>(arg_type));
                },
                [&](ElemSig::EnumValue enum_value)
                {
                    if (enum_value.type.m_typedef.TypeName() == "AttributeTargets")
                    {
                        std::vector<std::string> values;
                        auto value = std::get<uint32_t>(enum_value.value);
                        if (value == 4294967295)
                        {
                            values.emplace_back("All");
                        }
                        else
                        {
                            static struct
                            {
                                uint32_t value;
                                char const* name;
                            }
                            attribute_target_enums[] =
                            {
                                { 1, "Delegate" },
                                { 2, "Enum" },
                                { 4, "Event" },
                                { 8, "Field" },
                                { 16, "Interface" },
                                { 64, "Method" },
                                { 128, "Parameter" },
                                { 256, "Property" },
                                { 512, "Class" },   // "RuntimeClass"
                                { 1024, "Struct" },
                                { 2048, "All" },    // "InterfaceImpl"
                                { 8192, "Struct" }, // "ApiContract"
                            };
                            for (auto&& target_enum : attribute_target_enums)
                            {
                                if (value & target_enum.value)
                                {
                                    values.emplace_back(target_enum.name);
                                }
                            }
                        }
                        w.write("%", 
                            bind_list([](writer& w, auto&& value){ w.write("AttributeTargets.%", value); }, 
                                " | ", values));
                    }
                    else for (auto field : enum_value.type.m_typedef.FieldList())
                    {
                        if (field.Name() == "value__") continue;
                        auto field_value = field.Constant().Value();
                        if (std::visit([&](auto&& v) { return Constant::constant_type{ v } == field_value; }, enum_value.value))
                        {
                            w.write("%.%", 
                                bind<write_projection_type>(enum_value.type.m_typedef),
                                field.Name());
                        }
                    }
                },
                [&](std::string_view type_name)
                {
                    w.write("\"%\"", type_name);
                },
                [&](auto&&)
                {
                    if (auto uint32_value = std::get_if<uint32_t>(&arg_value.value))
                    {
                        w.write("%u", *uint32_value);
                    }
                    else if (auto int32_value = std::get_if<int32_t>(&arg_value.value))
                    {
                        w.write(*int32_value);
                    }
                    else if (auto uint64_value = std::get_if<uint64_t>(&arg_value.value))
                    {
                        w.write(*uint64_value);
                    }
                    else if (auto int64_value = std::get_if<int64_t>(&arg_value.value))
                    {
                        w.write(*int64_value);
                    }
                    else if (auto bool_value = std::get_if<bool>(&arg_value.value))
                    {
                        w.write(*bool_value ? "true" : "false");
                    }
                    else if (auto char_value = std::get_if<char16_t>(&arg_value.value))
                    {
                        w.write(*char_value);
                    }
                    else if (auto uint8_value = std::get_if<uint8_t>(&arg_value.value))
                    {
                        w.write(*uint8_value);
                    }
                    else if (auto int8_value = std::get_if<int8_t>(&arg_value.value))
                    {
                        w.write(*int8_value);
                    }
                    else if (auto uint16_value = std::get_if<uint16_t>(&arg_value.value))
                    {
                        w.write(*uint16_value);
                    }
                    else if (auto int16_value = std::get_if<int16_t>(&arg_value.value))
                    {
                        w.write(*int16_value);
                    }
                    else if (auto float_value = std::get_if<float>(&arg_value.value))
                    {
                        w.write_printf("f", *float_value);
                    }
                    else if (auto double_value = std::get_if<double>(&arg_value.value))
                    {
                        w.write_printf("f", *double_value);
                    }
                });
        };

        std::map<std::string, std::vector<std::string>> attributes;
        for (auto&& attribute : type.CustomAttribute())
        {
            auto [attribute_namespace, attribute_name] = attribute.TypeNamespaceAndName();
            attribute_name = attribute_name.substr(0, attribute_name.length() - "Attribute"sv.length());
            // Guid and Flags are handled explicitly
            if (attribute_name == "Guid" || attribute_name == "Flags") continue;
            auto attribute_full = (attribute_name == "AttributeUsage") ? "AttributeUsage" :
                w.write_temp("%.%", attribute_namespace, attribute_name);
            std::vector<std::string> params;
            auto signature = attribute.Value();
            for (auto&& arg : signature.FixedArgs())
            {
                params.push_back(w.write_temp("%", bind(write_fixed_arg, arg)));
            }
            for (auto&& arg : signature.NamedArgs())
            {
                params.push_back(w.write_temp("% = %", arg.name, bind(write_fixed_arg, arg.value)));
            }
            attributes[attribute_full] = std::move(params);
        }
        if (auto&& usage = attributes.find("AttributeUsage"); usage != attributes.end())
        {
            bool allow_multiple = attributes.find("Windows.Foundation.Metadata.AllowMultiple") != attributes.end();
            usage->second.push_back(w.write_temp("AllowMultiple = %", allow_multiple ? "true" : "false"));
        }

        for (auto&& attribute : attributes)
        {
            w.write("[");
            w.write(attribute.first);
            if (!attribute.second.empty())
            {
                w.write("(%)", bind_list(", ", attribute.second));
            }
            w.write("]\n");
        }
    }

    void write_contract(writer& w, TypeDef const& type)
    {
        auto type_name = write_type_name_temp(w, type);
        w.write(R"(%public enum %
{
}
)",
            bind<write_custom_attributes>(type),
            type_name);
    }

    void write_attribute(writer& w, TypeDef const& type)
    {
        auto type_name = write_type_name_temp(w, type);

        w.write(R"(%public sealed class %: Attribute
{
%}
)",
            bind<write_custom_attributes>(type),
            type_name,
            [&](writer& w)
            {
                auto methods = type.MethodList();
                for (auto&& method : methods)
                {
                    if (method.Name() != ".ctor") continue;
                    method_signature signature{ method };
                    w.write("public %(%){}\n",
                        type_name,
                        bind_list<write_projection_parameter>(", ", signature.params()));
                }
                for (auto&& field : type.FieldList())
                {
                    w.write("public % %;\n",
                        bind<write_projection_type>(get_type_semantics(field.Signature().Type())),
                        field.Name());
                }
            });
    }

    void write_interface(writer& w, TypeDef const& type)
    {
        XLANG_ASSERT(get_category(type) == category::interface_type);
        auto type_name = write_type_name_temp(w, type);

        uint32_t const vtable_base = type.MethodList().first.index();
        w.write(R"([global::WinRT.WindowsRuntimeType]
%
%% interface %%
{%
}
)",
            // Interface
            bind<write_guid_attribute>(type),
            bind<write_custom_attributes>(type),
            is_exclusive_to(type) ? "internal" : "public",
            type_name,
            bind<write_type_inheritance>(type, object_type{}, false),
            bind<write_interface_member_signatures>(type)
        );
    }

    bool write_abi_interface(writer& w, TypeDef const& type)
    {
        XLANG_ASSERT(get_category(type) == category::interface_type);
        auto type_name = write_type_name_temp(w, type, "%", true);
        auto nongenerics_class = w.write_temp("%_Delegates", bind<write_typedef_name>(type, true, false));
        auto is_generic = distance(type.GenericParam()) > 0;
        std::set<std::string> generic_methods;
        std::vector<std::string> nongeneric_delegates;

        uint32_t const vtable_base = type.MethodList().first.index();

        std::map<std::string, required_interface> required_interfaces;
        write_required_interface_members_for_abi_type(w, type, required_interfaces);

        w.write(R"([global::WinRT.ObjectReferenceWrapper(nameof(_obj))]
%
public class % : %
{
%
internal static ObjectReference<Vftbl> FromAbi(IntPtr thisPtr)%
public static implicit operator %(IObjectReference obj) => (obj != null) ? new %(obj) : null;
protected readonly ObjectReference<Vftbl> _obj;
public IObjectReference ObjRef { get => _obj; }
public IntPtr ThisPtr => _obj.ThisPtr;
public ObjectReference<I> AsInterface<I>() => _obj.As<I>();
public A As<A>() => _obj.AsType<A>();
public @(IObjectReference obj) : this(obj.As<Vftbl>()) {}
internal @(ObjectReference<Vftbl> obj)
{
_obj = obj;%
%}
%%%%}
)",
            // Interface abi implementation
            bind<write_guid_attribute>(type),
            type_name,
            bind<write_type_name>(type, false, false),
            // Vftbl
            bind<write_vtable>(type, type_name, generic_methods, nongenerics_class, nongeneric_delegates),
            // Interface impl
            [&](writer& w) {
                if (!is_generic)
                {
                    w.write(" => ObjectReference<Vftbl>.FromAbi(thisPtr);\n");
                    return;
                }
                w.write(R"(
{
if (thisPtr == IntPtr.Zero)
{
return null;
}
var vftblT = new Vftbl(thisPtr);
return ObjectReference<Vftbl>.FromAbi(thisPtr, vftblT.IInspectableVftbl.IUnknownVftbl, vftblT);
}
public static Guid PIID = Vftbl.PIID;
)");
            },
            type_name,
            type_name,
            type.TypeName(),
            type.TypeName(),
            bind<write_event_source_ctors>(type),
            [&](writer& w) {
                for (auto required_interface : required_interfaces)
                {
                    if (required_interface.second.helper_wrapper.empty()) 
                        continue;
                    w.write("% = new %.FromAbiHelper(ObjRef);\n", 
                        required_interface.second.adapter,
                        required_interface.second.helper_wrapper);
                }
            },
            [&](writer& w) {
                for (auto required_interface : required_interfaces)
                {
                    if (required_interface.second.helper_wrapper.empty())
                        continue;
                    w.write("%.FromAbiHelper %;\n",
                        required_interface.second.helper_wrapper,
                        required_interface.second.adapter);
                }
            },
            bind<write_interface_members>(type, generic_methods),
            bind<write_event_sources>(type),
            [&](writer& w) {
                for (auto required_interface : required_interfaces)
                {
                    w.write("%", required_interface.second.members);
                }
            }
        );

        if (!nongeneric_delegates.empty())
        {
            w.write(R"([global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
public static class %
{
%}
)",
                nongenerics_class,
                bind_each(nongeneric_delegates));
        }
        w.write("\n");

        return true;
    }


    void write_class(writer& w, TypeDef const& type)
    {
        if (is_static(type))
        {
            write_static_class(w, type);
            return;
        }

        auto type_name = write_type_name_temp(w, type);
        auto default_interface_name = get_default_interface_name(w, type, false);
        auto default_interface_abi_name = get_default_interface_name(w, type, true);
        auto base_semantics = get_type_semantics(type.Extends());
        auto derived_new = std::holds_alternative<object_type>(base_semantics) ? "" : "new ";

        w.write(R"([global::WinRT.WindowsRuntimeType]
[global::WinRT.ProjectedRuntimeClass(nameof(_default))]
%public %class %%, IEquatable<%>
{
public %IntPtr ThisPtr => _default.ThisPtr;

private IObjectReference _inner = null;
private readonly Lazy<%> _defaultLazy;
private readonly Dictionary<Type, object> _lazyInterfaces;

private % _default => _defaultLazy.Value;
%
public static %% FromAbi(IntPtr thisPtr)
{
if (thisPtr == IntPtr.Zero) return null;
var obj = MarshalInspectable.FromAbi(thisPtr);
return obj is % ? (%)obj : new %((%)obj);
}

% %(% ifc)%
{
_defaultLazy = new Lazy<%>(() => ifc);
_lazyInterfaces = new Dictionary<Type, object>()
{%
};
}

public static bool operator ==(% x, % y) => (x?.ThisPtr ?? IntPtr.Zero) == (y?.ThisPtr ?? IntPtr.Zero);
public static bool operator !=(% x, % y) => !(x == y);
public bool Equals(% other) => this == other;
public override bool Equals(object obj) => obj is % that && this == that;
public override int GetHashCode() => ThisPtr.GetHashCode();
%

private struct InterfaceTag<I>{};

private % AsInternal(InterfaceTag<%> _) => _default;
%%
}
)",
            bind<write_custom_attributes>(type),
            bind<write_class_modifiers>(type),
            type_name,
            bind<write_type_inheritance>(type, base_semantics, true),
            type_name,
            derived_new,
            default_interface_abi_name,
            default_interface_abi_name,
            bind<write_attributed_types>(type),
            derived_new,
            type_name,
            type_name,
            type_name,
            type_name,
            default_interface_abi_name,
            type.Flags().Sealed() ? "internal" : "protected internal",
            type_name,
            default_interface_abi_name,
            bind<write_base_constructor_dispatch>(base_semantics),
            default_interface_abi_name,
            bind<write_lazy_interface_initialization>(type),
            type_name,
            type_name,
            type_name,
            type_name,
            type_name,
            type_name,
            bind([&](writer& w)
            {
                bool has_base_type = !std::holds_alternative<object_type>(get_type_semantics(type.Extends()));
                if (!type.Flags().Sealed())
                {
                    w.write(R"(
protected %(global::WinRT.DerivedComposed _)%
{
_defaultLazy = new Lazy<%>(() => GetDefaultReference<%.Vftbl>());
})",
type.TypeName(),
has_base_type ? ":base(_)" : "",
default_interface_abi_name,
default_interface_abi_name);
                }


                std::string_view access_spec = "protected ";
                std::string_view override_spec = has_base_type ? "override " : "virtual ";

                if (type.Flags().Sealed() && !has_base_type)
                {
                    access_spec = "private ";
                    override_spec = " ";
                }

                w.write(R"(
%%IObjectReference GetDefaultReference<T>() => _default.AsInterface<T>();)",
                    access_spec,
                    override_spec);

                w.write(R"(
%%IObjectReference GetReferenceForQI() => _inner ?? _default.ObjRef;)",
                    access_spec,
                    override_spec);
            }),
            default_interface_name,
            default_interface_name,
            bind<write_class_members>(type),
            bind([&](writer& w)
            {
                    bool has_base_class = !std::holds_alternative<object_type>(get_type_semantics(type.Extends()));
                    separator s{ w, " || " };
                    w.write(R"(
%bool IsOverridableInterface(Guid iid) => %%;

global::System.Runtime.InteropServices.CustomQueryInterfaceResult global::System.Runtime.InteropServices.ICustomQueryInterface.GetInterface(ref Guid iid, out IntPtr ppv)
{
ppv = IntPtr.Zero;
if (IsOverridableInterface(iid) || typeof(global::WinRT.IInspectable).GUID == iid)
{
return global::System.Runtime.InteropServices.CustomQueryInterfaceResult.NotHandled;
}

if (GetReferenceForQI().TryAs<IUnknownVftbl>(iid, out ObjectReference<IUnknownVftbl> objRef) >= 0)
{
using (objRef)
{
ppv = objRef.GetRef();
return global::System.Runtime.InteropServices.CustomQueryInterfaceResult.Handled;
}
}

return global::System.Runtime.InteropServices.CustomQueryInterfaceResult.NotHandled;
})",
                bind([&](writer& w)
                {
                    auto visibility = "protected ";
                    auto overridable = "virtual ";
                    if (has_base_class)
                    {
                        overridable = "override ";
                    }
                    else if (type.Flags().Sealed())
                    {
                        visibility = "private ";
                        overridable = "";
                    }
                    w.write(visibility);
                    w.write(overridable);
                }),
                bind_each([&](writer& w, InterfaceImpl const& iface)
                {
                    if (has_attribute(iface, "Windows.Foundation.Metadata", "OverridableAttribute"))
                    {
                        s();
                        w.write("GuidGenerator.GetIID(typeof(%)) == iid",
                            bind<write_type_name>(get_type_semantics(iface.Interface()), false, false));
                    }
                }, type.InterfaceImpl()),
                bind([&](writer& w)
                {
                    if (has_base_class)
                    {
                        s();
                        w.write("base.IsOverridableInterface(iid)");
                    }
                    if (s.first)
                    {
                        w.write("false");
                    }
                }));
            }));
    }

    void write_abi_class(writer& w, TypeDef const& type)
    {
        if (is_static(type))
        {
            return;
        }

        auto abi_type_name = write_type_name_temp(w, type, "%", true);
        auto projected_type_name = write_type_name_temp(w, type);
        auto default_interface_abi_name = get_default_interface_name(w, type, true);

        w.write(R"([global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
public struct %
{
public static IObjectReference CreateMarshaler(% obj) => obj is null ? null : MarshalInspectable.CreateMarshaler(obj).As<%.Vftbl>();
public static IntPtr GetAbi(IObjectReference value) => value is null ? IntPtr.Zero : MarshalInterfaceHelper<object>.GetAbi(value);
public static % FromAbi(IntPtr thisPtr) => %.FromAbi(thisPtr);
public static IntPtr FromManaged(% obj) => obj is null ? IntPtr.Zero : CreateMarshaler(obj).GetRef();
public static unsafe MarshalInterfaceHelper<%>.MarshalerArray CreateMarshalerArray(%[] array) => MarshalInterfaceHelper<%>.CreateMarshalerArray(array, (o) => CreateMarshaler(o));
public static (int length, IntPtr data) GetAbiArray(object box) => MarshalInterfaceHelper<%>.GetAbiArray(box);
public static unsafe %[] FromAbiArray(object box) => MarshalInterfaceHelper<%>.FromAbiArray(box, FromAbi);
public static (int length, IntPtr data) FromManagedArray(%[] array) => MarshalInterfaceHelper<%>.FromManagedArray(array, (o) => FromManaged(o));
public static void DisposeMarshaler(IObjectReference value) => MarshalInspectable.DisposeMarshaler(value);
public static void DisposeMarshalerArray(MarshalInterfaceHelper<%>.MarshalerArray array) => MarshalInterfaceHelper<%>.DisposeMarshalerArray(array);
public static void DisposeAbi(IntPtr abi) => MarshalInspectable.DisposeAbi(abi);
public static unsafe void DisposeAbiArray(object box) => MarshalInspectable.DisposeAbiArray(box);
}
)",
            abi_type_name,
            projected_type_name,
            default_interface_abi_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name,
            projected_type_name);
    }

    void write_delegate(writer& w, TypeDef const& type)
    {
        method_signature signature{ get_delegate_invoke(type) };
        w.write(R"([global::WinRT.WindowsRuntimeType]
%public delegate % %(%);
)",
            bind<write_custom_attributes>(type),
            bind<write_projection_return_type>(signature),
            bind<write_type_name>(type, false, false),
            bind_list<write_projection_parameter>(", ", signature.params()));
    }

    void write_abi_delegate(writer& w, TypeDef const& type)
    {
        auto method = get_delegate_invoke(type);
        method_signature signature{ method };
        auto type_name = write_type_name_temp(w, type);
        auto type_params = w.write_temp("%", bind<write_type_params>(type));
        auto is_generic = distance(type.GenericParam()) > 0;
        auto generic_abi_types = get_generic_abi_types(w, signature);
        bool have_generic_params = std::find_if(generic_abi_types.begin(), generic_abi_types.end(),
            [](auto&& pair){ return !pair.second.empty(); }) != generic_abi_types.end();

        w.write(R"([global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
%
public static class @%
{%
%
private static readonly global::WinRT.Interop.IDelegateVftbl AbiToProjectionVftable;
public static readonly IntPtr AbiToProjectionVftablePtr;

static @()
{
AbiInvokeDelegate = %;
AbiToProjectionVftable = new global::WinRT.Interop.IDelegateVftbl
{
IUnknownVftbl = global::WinRT.Interop.IUnknownVftbl.AbiToProjectionVftbl,
Invoke = Marshal.GetFunctionPointerForDelegate(AbiInvokeDelegate)
};
var nativeVftbl = ComWrappersSupport.AllocateVtableMemory(typeof(@%), Marshal.SizeOf<global::WinRT.Interop.IDelegateVftbl>());
Marshal.StructureToPtr(AbiToProjectionVftable, nativeVftbl, false);
AbiToProjectionVftablePtr = nativeVftbl;
}

public static global::System.Delegate AbiInvokeDelegate { get ; }

public static unsafe IObjectReference CreateMarshaler(% managedDelegate) => 
managedDelegate is null ? null : ComWrappersSupport.CreateCCWForObject(managedDelegate).As<global::WinRT.Interop.IDelegateVftbl>(GuidGenerator.GetIID(typeof(@%)));

public static IntPtr GetAbi(IObjectReference value) => MarshalInterfaceHelper<%>.GetAbi(value);

public static unsafe % FromAbi(IntPtr nativeDelegate)
{
var abiDelegate = ObjectReference<IDelegateVftbl>.FromAbi(nativeDelegate);
return abiDelegate is null ? null : (%)ComWrappersSupport.TryRegisterObjectForInterface(new %(new NativeDelegateWrapper(abiDelegate).Invoke), nativeDelegate);
}

[global::WinRT.ObjectReferenceWrapper(nameof(_nativeDelegate))]
private class NativeDelegateWrapper
{
private readonly ObjectReference<global::WinRT.Interop.IDelegateVftbl> _nativeDelegate;
private readonly AgileReference _agileReference = default;

public NativeDelegateWrapper(ObjectReference<global::WinRT.Interop.IDelegateVftbl> nativeDelegate)
{
_nativeDelegate = nativeDelegate;
if (_nativeDelegate.TryAs<ABI.WinRT.Interop.IAgileObject.Vftbl>(out var objRef) < 0)
{
_agileReference = new AgileReference(_nativeDelegate);
}
else
{
objRef.Dispose();
}
}

public % Invoke(%)
{
using var agileDelegate = _agileReference?.Get()?.As<global::WinRT.Interop.IDelegateVftbl>(GuidGenerator.GetIID(typeof(@%))); 
var delegateToInvoke = agileDelegate ?? _nativeDelegate;
IntPtr ThisPtr = delegateToInvoke.ThisPtr;
var abiInvoke = Marshal.GetDelegateForFunctionPointer%(delegateToInvoke.Vftbl.Invoke%);%
}
}

public static IntPtr FromManaged(% managedDelegate) => CreateMarshaler(managedDelegate)?.GetRef() ?? IntPtr.Zero;

public static void DisposeMarshaler(IObjectReference value) => MarshalInterfaceHelper<%>.DisposeMarshaler(value);

public static void DisposeAbi(IntPtr abi) => MarshalInterfaceHelper<%>.DisposeAbi(abi);

private static unsafe int Do_Abi_Invoke%
{
%
}
}

)",
            bind<write_guid_attribute>(type),
            type.TypeName(),
            type_params,
            [&](writer& w) {
                if (type_params.empty()) return;
                w.write(R"(
public static Guid PIID = GuidGenerator.CreateIID(typeof(%));)",
                    type_name
                );
            },
            [&](writer& w) {
                if (!is_generic)
                {
                    w.write("private unsafe delegate int Abi_Invoke(%);\n",
                        bind<write_abi_parameters>(signature));
                    return;
                }
                w.write(R"(private static readonly Type Abi_Invoke_Type = Expression.GetDelegateType(new Type[] { typeof(void*), %typeof(int) });
)",
                    bind_each([&](writer& w, auto&& pair)
                    {
                        w.write("%, ", pair.first);
                    }, generic_abi_types));
            },
            // class constructor
            type.TypeName(),
            [&](writer& w) {
                if (!is_generic)
                {
                    w.write("new Abi_Invoke(Do_Abi_Invoke)");
                    return;
                }
                w.write("global::System.Delegate.CreateDelegate(Abi_Invoke_Type, typeof(@%).GetMethod(nameof(Do_Abi_Invoke), BindingFlags.Static | BindingFlags.NonPublic)%)",
                    type.TypeName(),
                    type_params,
                    [&](writer& w) {
                        if (!have_generic_params) return;
                        w.write(".MakeGenericMethod(new Type[]{ % })\n",
                            [&](writer& w) {
                                int count = 0;
                                for (auto&& pair : generic_abi_types)
                                {
                                    if (pair.second.empty()) continue;
                                    w.write(count++ == 0 ? "" : ", ");
                                    w.write(pair.first);
                                }
                            });
                    });
            },
            type.TypeName(),
            type_params,
            // CreateMarshaler
            type_name,
            type.TypeName(),
            type_params,
            // GetAbi
            type_name,
            // FromAbi
            type_name,
            type_name,
            type_name,
            // NativeDelegateWrapper.Invoke
            bind<write_projection_return_type>(signature),
            bind_list<write_projection_parameter>(", ", signature.params()),
            type.TypeName(),
            type_params,
            is_generic ? "" : "<Abi_Invoke>",
            is_generic ? ", Abi_Invoke_Type" : "",
            bind<write_abi_method_call>(signature, "abiInvoke", is_generic, false),
            // FromManaged
            type_name,
            // DisposeMarshaler
            type_name,
            // DisposeAbi
            type_name,
            // Do_Abi_Invoke
            [&](writer& w) {
                if (!is_generic)
                {
                    w.write("(%)", bind<write_abi_parameters>(signature));
                    return;
                }
                if (have_generic_params)
                {
                    w.write("<");
                    int count = 0;
                    for (auto&& pair : generic_abi_types)
                    {
                        if (pair.second.empty()) continue;
                        w.write(count++ == 0 ? "" : ", ");
                        w.write(pair.second);
                    }
                    w.write(">");
                }
                w.write("(void* thisPtr");
                int index = 0;
                for (auto&& param : signature.params())
                {
                    auto generic_type = generic_abi_types[index++].second;
                    auto param_cat = get_param_category(param);
                    if (!generic_type.empty() && (param_cat <= param_category::out))
                    {
                        w.write(", %% %",
                            param_cat == param_category::out ? "out " : "",
                            generic_type,
                            bind<write_parameter_name>(param));
                    }
                    else
                    {
                        write_abi_parameter(w, param);
                    }
                }
                if (auto return_sig = signature.return_signature())
                {
                    auto generic_type = generic_abi_types[index++].second;
                    if (!return_sig.Type().is_szarray() && !generic_type.empty())
                    {
                        w.write(", out % %", generic_type, signature.return_param_name());
                    }
                    else
                    {
                        write_abi_return(w, signature);
                    }
                }
                w.write(")");
            },
            bind<write_managed_method_call>(signature,
                w.write_temp(R"(global::WinRT.ComWrappersSupport.MarshalDelegateInvoke(%, (% invoke) =>
{
    %
}))",
                    is_generic ? "new IntPtr(thisPtr)" : "thisPtr",
                    is_generic ? "global::System.Delegate" : type_name,
                    bind([&](writer& w)
                    {
                        if (is_generic)
                        {
                            w.write(R"(invoke.DynamicInvoke(%);)", "%");
                        }
                        else if (signature.return_signature())
                        {
                            w.write("return invoke(%);", "%");
                        }
                        else
                        {
                            w.write("invoke(%);");
                        }
                    })
        )));
    }

    void write_constant(writer& w, Constant const& value)
    {
        switch (value.Type())
        {
        case ConstantType::Int32:
            w.write_printf("%#0x", value.ValueInt32());
            break;
        case ConstantType::UInt32:
            w.write_printf("%#0x", value.ValueUInt32());
            break;
        }
    }

    void write_enum(writer& w, TypeDef const& type)
    {
        if (is_flags_enum(type))
        {
            w.write("[FlagsAttribute]\n");
        }

        auto enum_underlying_type = is_flags_enum(type) ? "uint" : "int";

        w.write(R"([global::WinRT.WindowsRuntimeType]
%public enum % : %
{
)", 
        bind<write_custom_attributes>(type),
        bind<write_type_name>(type, false, false), enum_underlying_type);
        {
            for (auto&& field : type.FieldList())
            {
                if (auto constant = field.Constant())
                {
                    w.write("% = unchecked((%)%),\n", field.Name(), enum_underlying_type, bind<write_constant>(constant));
                }
            }
        }
        w.write("}\n");
    }

    void write_struct(writer& w, TypeDef const& type)
    {
        auto name = w.write_temp("%", bind<write_type_name>(type, false, false));

        struct field_info
        {
            std::string type;
            std::string name;
            bool is_interface;
        };
        std::vector<field_info> fields;
        for (auto&& field : type.FieldList())
        {
            auto semantics = get_type_semantics(field.Signature().Type());
            field_info field_info{};
            field_info.type = w.write_temp("%", [&](writer& w){ write_projection_type(w, semantics); });
            field_info.name = field.Name();
            if (auto td = std::get_if<type_definition>(&semantics))
            {
                field_info.is_interface = get_category(*td) == category::interface_type;
            }
            else if (auto gti = std::get_if<generic_type_instance>(&semantics))
            {
                field_info.is_interface = get_category(gti->generic_type) == category::interface_type;
            }
            fields.emplace_back(field_info);
        }

        w.write(R"([global::WinRT.WindowsRuntimeType]
%public struct %: IEquatable<%>
{
%
public %(%)
{
%
}

public static bool operator ==(% x, % y) => %;
public static bool operator !=(% x, % y) => !(x == y);
public bool Equals(% other) => this == other;
public override bool Equals(object obj) => obj is % that && this == that;
public override int GetHashCode() => %;
}
)",
            // struct
            bind<write_custom_attributes>(type),
            name,
            name,
            bind_each([](writer& w, auto&& field)
            {
                w.write("public % %;\n", field.type, field.name);
            }, fields),
            // ctor
            name,
            bind_list([](writer& w, auto&& field)
            {
                w.write("% _%", field.type, field.name);
            }, ", ", fields),
            bind_each([](writer& w, auto&& field)
            {
                w.write("% = _%; ", field.name, field.name);
            }, fields),
            // ==
            name,
            name,
            bind_list([](writer& w, auto&& field)
            {
                w.write("x.% == y.%", 
                    field.name, field.name);
            }, " && ", fields),
            // !=, Equals
            name,
            name,
            name,
            name,
            // GetHashCode
            bind_list([](writer& w, auto&& field)
            {
                w.write("%.GetHashCode()", field.name);
            }, " ^ ", fields)
        );
    }

    void write_abi_struct(writer& w, TypeDef const& type)
    {
        if (is_type_blittable(type))
        {
            return;
        }

        w.write("[global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\npublic struct %\n{\n", bind<write_type_name>(type, true, false));
        for (auto&& field : type.FieldList())
        {
            w.write("public ");
            write_abi_type(w, get_type_semantics(field.Signature().Type()));
            w.write(" %;\n", field.Name());
        }

        auto projected_type = w.write_temp("%", bind<write_projection_type>(type));
        auto abi_type = w.write_temp("%", bind<write_type_name>(type, true, false));

        std::vector<abi_marshaler> marshalers;
        for (auto&& field : type.FieldList())
        {
            abi_marshaler m{ std::string(field.Name()), -1 };
            set_abi_marshaler(w, field.Signature().Type(), m);
            marshalers.push_back(std::move(m));
        }

        // blittable: (no marshaler) value type requiring no marshaling/disposing 
        // marshalable: (marshaler, is_value_type) value type requiring only marshaling, no disposing
        // disposable: (marshaler, !is_value_type) ref type requiring marshaling and disposing
        bool have_disposers = std::find_if(marshalers.begin(), marshalers.end(), [](abi_marshaler const& m)
        {
            return !m.is_value_type;
        }) != marshalers.end();

        w.write(R"(
internal struct Marshaler
{
%public % __abi;
)",
            bind_each([](writer& w, abi_marshaler const& m)
            {
                if (m.marshaler_type.empty()) return;
                w.write("public % _%;\n", m.local_type, m.param_name);
            }, marshalers),
            abi_type);
        if (have_disposers)
        {
            w.write(R"(public void Dispose()
{
%}
)",
                bind_each([](writer& w, abi_marshaler const& m)
                {
                    if(m.is_value_type) return;
                    w.write("%.DisposeMarshaler(_%);\n",
                        m.marshaler_type,
                        m.param_name);
                }, marshalers));
        }
        w.write("}\n");

        w.write(R"(
internal static Marshaler CreateMarshaler(% arg)
{
var m = new Marshaler();)",
            projected_type);
        if (have_disposers)
        {
            w.write(R"(
Func<bool> dispose = () => { m.Dispose(); return false; };
try
{)");
        }
        for (auto&& m : marshalers)
        {
            if (m.marshaler_type.empty()) continue;
            w.write("\nm._% = ", m.param_name);
            m.write_create(w, "arg." + m.get_escaped_param_name(w));
            w.write(";");
        }
        w.write(R"(
m.__abi = new %()
{
%};
return m;)",
            abi_type,
            [&](writer& w)
            {
                int count = 0;
                for (auto&& m : marshalers)
                {
                    w.write(count++ == 0 ? "" : ", ");
                    if (m.marshaler_type.empty())
                    {
                        std::string format;
                        if (m.param_type == "bool")
                        {
                            format = "% = (byte)(arg.% ? 1 : 0)\n";
                        }
                        else if (m.param_type == "char")
                        {
                            format = "% = (ushort)arg.%\n";
                        }
                        else
                        {
                            format = "% = arg.%\n";
                        }
                        w.write(format,
                            m.get_escaped_param_name(w),
                            m.get_escaped_param_name(w));
                        continue;
                    }
                    w.write("% = %.GetAbi(m._%)\n",
                        m.get_escaped_param_name(w),
                        m.marshaler_type,
                        m.param_name);
                }
            });
        if (have_disposers)
        {
            w.write(R"(
}
catch (Exception) when (dispose())
{
// Will never execute
return default;
}
)");
        }
        w.write("}\n");

        w.write(R"(
internal static % GetAbi(Marshaler m) => m.__abi;
)",
            abi_type);

        w.write(R"(
public static % FromAbi(% arg)
{
return new %()
{
%};
}
)",
            projected_type,
            abi_type,
            projected_type,
            [&](writer& w)
            {
                int count = 0;
                for (auto&& m : marshalers)
                {
                    w.write(count++ == 0 ? "" : ", ");
                    if (m.marshaler_type.empty())
                    {
                        std::string format;
                        if (m.param_type == "bool")
                        {
                            format = "% = arg.% != 0\n";
                        }
                        else if (m.param_type == "char")
                        {
                            format = "% = (char)arg.%\n";
                        }
                        else
                        {
                            format = "% = arg.%\n";
                        }
                        w.write(format,
                            m.get_escaped_param_name(w),
                            m.get_escaped_param_name(w));
                        continue;
                    }
                    w.write("% = %\n",
                        m.get_escaped_param_name(w),
                        [&](writer& w) {m.write_from_abi(w, "arg." + m.get_escaped_param_name(w)); });
                }
            });

        w.write(R"(
public static % FromManaged(% arg)
{
return new %()
{
%};
}
)",
            abi_type,
            projected_type,
            abi_type,
            [&](writer& w)
            {
                int count = 0;
                for (auto&& m : marshalers)
                {
                    w.write(count++ == 0 ? "" : ", ");
                    if (m.marshaler_type.empty())
                    {
                        std::string format;
                        if (m.param_type == "bool")
                        {
                            format = "% = (byte)(arg.% ? 1 : 0)\n";
                        }
                        else if (m.param_type == "char")
                        {
                            format = "% = (ushort)arg.%\n";
                        }
                        else
                        {
                            format = "% = arg.%\n";
                        }
                        w.write(format,
                            m.get_escaped_param_name(w),
                            m.get_escaped_param_name(w));
                        continue;
                    }
                    w.write("% = %\n",
                        m.get_escaped_param_name(w), [&](writer& w) {
                            m.write_from_managed(w, "arg." + m.get_escaped_param_name(w)); });
                }
            });

        w.write(R"(
internal static unsafe void CopyAbi(Marshaler arg, IntPtr dest) => 
    *(%*)dest.ToPointer() = GetAbi(arg);
)",
            abi_type);

        w.write(R"(
public static unsafe void CopyManaged(% arg, IntPtr dest) =>
    *(%*)dest.ToPointer() = FromManaged(arg);
)",
            projected_type,
            abi_type);
    
      w.write(R"(
internal static void DisposeMarshaler(Marshaler m) %
)",
            have_disposers ? "=> m.Dispose();" : "{}");

        w.write(R"(
public static void DisposeAbi(% abi){ /*todo*/ }
}

)",
            abi_type);
    }
}