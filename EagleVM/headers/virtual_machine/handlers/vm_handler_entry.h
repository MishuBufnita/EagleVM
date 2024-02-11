#pragma once
#include <optional>
#include <array>
#include <functional>

#include "util/util.h"
#include "util/zydis_helper.h"
#include "util/section/function_container.h"

#include "virtual_machine/handlers/vm_handler_context.h"
#include "virtual_machine/base_instruction_virtualizer.h"

// im not proud of this but this is the easiest way i can do this dependency injection shenanigans
#define CREATE_HANDLER_CTOR(x, ...) x::x(vm_register_manager* manager, vm_handler_generator* handler_generator) \
    : vm_handler_entry(manager, handler_generator) \
    { supported_sizes = {__VA_ARGS__}; };

class code_label;
class vm_handler_entry : public base_instruction_virtualizer
{
public:
    explicit vm_handler_entry(vm_register_manager* manager, vm_handler_generator* handler_generator);

    code_label* get_handler_va(reg_size size) const;

    void setup_labels();
    function_container construct_handler();

protected:
    inline static uint8_t vm_overhead = 20;
    inline static uint8_t stack_regs = 17;

    bool has_builder_hook;
    bool is_vm_handler;
    function_container container;

    std::vector<reg_size> supported_sizes;
    std::unordered_map<reg_size, code_label*> supported_handlers;

    virtual void construct_single(function_container& container, reg_size size) = 0;
};