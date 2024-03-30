#include "eaglevm-core/virtual_machine/vm_virtualizer.h"
#include "eaglevm-core/virtual_machine/handlers/handler/vm_handler_entry.h"
#include "eaglevm-core/virtual_machine/handlers/handler/inst_handler_entry.h"

#include "eaglevm-core/util/random.h"

namespace eagle::virt
{
    vm_virtualizer::vm_virtualizer(vm_inst* virtual_machine)
    {
        zydis_helper::setup_decoder();
        vm_inst_ = virtual_machine;
        hg_ = vm_inst_->get_handlers();
        rm_ = vm_inst_->get_regs();
    }

    vm_virtualizer::~vm_virtualizer()
    {
        // for the time being until i figure out how to garbage collect labels
        // for (auto& [block, label]: block_labels_)
        // {
        //     if (label)
        //         delete label;
        // }
    }

    std::vector<asmbl::function_container> vm_virtualizer::virtualize_segment(dasm::segment_dasm* dasm)
    {
        for (dasm::basic_block* block : dasm->blocks)
        {
            std::string block_name = "block:" + std::to_string(block->start_rva);
            block_labels_[block] = asmbl::code_label::create(block_name);
        }

        std::vector<asmbl::function_container> virtualized_blocks;
        for (dasm::basic_block* block : dasm->blocks)
        {
            asmbl::function_container block_container = virtualize_block(dasm, block);
            virtualized_blocks.push_back(block_container);
        }

        return virtualized_blocks;
    }

    asmbl::function_container vm_virtualizer::virtualize_block(dasm::segment_dasm* dasm, dasm::basic_block* block)
    {
        asmbl::function_container container{};
        container.assign_label(block_labels_[block]);

        uint64_t current_rva = block->start_rva;
        bool current_vm = false;

        int skip = 0;
        if (block->get_end_reason() != dasm::block_end)
            skip = -1;

        for (int i = 0; i < block->decoded_insts.size() + skip; i++)
        {
            zydis_decode& instruction = block->decoded_insts[i];

            auto [virt_status, instructions] = translate_to_virtual(instruction, current_rva);
            if (virt_status)
            {
                // check if we are already inside of virtual machine to prevent multiple enters
                if (!current_vm)
                {
                    asmbl::code_label* vmenter_return_label = asmbl::code_label::create("vmenter_return:" + current_rva);
                    call_vm_enter(container, vmenter_return_label);
                    container.assign_label(vmenter_return_label);

                    current_vm = true;
                }

                container.merge(instructions);
            }
            else
            {
                // exit virtual machine if this is a non-virtual instruction
                if (current_vm)
                {
                    asmbl::code_label* jump_label = asmbl::code_label::create("vmleave_dest:" + current_rva);
                    call_vm_exit(container, jump_label);
                    container.assign_label(jump_label);

                    current_vm = false;
                }

                if (zydis_helper::has_relative_operand(instruction))
                {
                    auto [target_address, op_i] = zydis_helper::calc_relative_rva(instruction, current_rva);
                    container.add([instruction, target_address, op_i](const uint32_t rva)
                    {
                        zydis_encoder_request encode_request = zydis_helper::decode_to_encode(instruction);
                        auto& op = encode_request.operands[op_i];

                        switch (op.type)
                        {
                            case ZYDIS_OPERAND_TYPE_MEMORY:
                            {
                                // needs to handle where mem and base have no registers
                                op.mem.displacement = target_address - rva;
                                break;
                            }
                            case ZYDIS_OPERAND_TYPE_IMMEDIATE:
                            {
                                op.imm.s = target_address - rva;
                                break;
                            }
                            default:
                            {
                                __debugbreak();
                            }
                        }

                        return encode_request;
                    });
                }
                else
                {
                    container.add(zydis_helper::decode_to_encode(instruction));
                }
            }

            current_rva += instruction.instruction.length;
        }

        // exit vm
        if (current_vm)
        {
            asmbl::code_label* jump_label = asmbl::code_label::create("vmexit dest: " + current_rva, true);
            call_vm_exit(container, jump_label);

            // vmexit will go to this label which is past the vmexit setup instructions
            container.assign_label(jump_label);
        }

        const dasm::block_end_reason end_reason = block->get_end_reason();
        switch (end_reason)
        {
            case dasm::block_end:
            {
                // simple block end, go to next rva after the instruction
                asmbl::code_label* jump_label = nullptr;

                auto [target, type] = dasm->get_jump(block);
                if (type == dasm::jump_outside_segment)
                {
                    // we return back to .text
                    jump_label = asmbl::code_label::create("vmleave_dest:" + target, true);
                    jump_label->finalize(target);
                }
                else
                {
                    // we are still inside the segment
                    dasm::basic_block* next_block = dasm->get_block(target);

                    jump_label = block_labels_[next_block];
                }

                create_vm_jump(ZYDIS_MNEMONIC_JMP, container, jump_label);
                break;
            }
            case dasm::block_jump:
            {
                // block_jump should only ever have 1 target_rvas
                auto [target, type] = dasm->get_jump(block);
                if (type == dasm::jump_outside_segment)
                {
                    // this means we are exiting our virtualized block, jump back to .text
                    asmbl::code_label* jump_label = asmbl::code_label::create("vmleave_dest:" + target);
                    jump_label->finalize(target);

                    create_vm_jump(ZYDIS_MNEMONIC_JMP, container, jump_label);
                }
                else
                {
                    dasm::basic_block* next_block = dasm->get_block(target);

                    // we are still inside the segment, jump to target block
                    create_vm_jump(ZYDIS_MNEMONIC_JMP, container, block_labels_[next_block]);
                }

                break;
            }
            case dasm::block_conditional_jump:
            {
                // this is a conditional jump

                // there could be different types of final blocks, but we just checked if the explicit one is present
                // case 1: conditional jump goes to outside rva
                {
                    const zydis_decode& l_decode = block->decoded_insts.back();

                    auto [target, type] = dasm->get_jump(block);
                    if (type == dasm::jump_outside_segment)
                    {
                        // we are still inside the segment, so we do a conditional jump if we want to leave
                        asmbl::code_label* jump_label = asmbl::code_label::create("vmleave_dest:" + target);
                        jump_label->finalize(target);

                        auto target_mneominc = l_decode.instruction.mnemonic;
                        create_vm_jump(target_mneominc, container, jump_label);
                    }
                    else
                    {
                        // we are still in the segment, so we do a conditional jump to a virtualized basic block
                        dasm::basic_block* next_block = dasm->get_block(target);

                        auto target_mneominc = l_decode.instruction.mnemonic;
                        create_vm_jump(target_mneominc, container, block_labels_[next_block]);
                    }
                }

                // case 2: conditional jump fails and goes to next block
                {
                    auto [target, type] = dasm->get_jump(block, true);
                    if (type == dasm::jump_outside_segment)
                    {
                        // the next block is outside of this segment, we just do a normal jump
                        asmbl::code_label* jump_label = asmbl::code_label::create("vmleave_dest:" + target);
                        jump_label->finalize(target);

                        create_vm_jump(ZYDIS_MNEMONIC_JMP, container, jump_label);
                    }
                    else
                    {
                        // the next block is inside this segment, so its virtualized
                        dasm::basic_block* next_block = dasm->get_block(target);

                        create_vm_jump(ZYDIS_MNEMONIC_JMP, container, block_labels_[next_block]);
                    }
                }

                break;
            }
        }

        return container;
    }

    asmbl::code_label* vm_virtualizer::get_label(dasm::basic_block* block)
    {
        if (block_labels_.contains(block))
            return block_labels_[block];

        return nullptr;
    }

    void vm_virtualizer::call_vm_enter(asmbl::function_container& container, asmbl::code_label* target)
    {
        const handle::vm_handler_entry* vmenter = vm_inst_->get_handlers()->v_handlers[MNEMONIC_VM_ENTER];
        const auto vmenter_address = vmenter->get_vm_handler_va(bit64);

        container.add(RECOMPILE(zydis_helper::enc(ZYDIS_MNEMONIC_PUSH, ZLABEL(target))));

        asmbl::code_label* rel_label = asmbl::code_label::create("call_vm_enter_rel");
        container.add(rel_label, RECOMPILE(zydis_helper::enc(ZYDIS_MNEMONIC_JMP, ZJMP(vmenter_address, rel_label))));
    }

    void vm_virtualizer::call_vm_exit(asmbl::function_container& container, asmbl::code_label* target)
    {
        const handle::vm_handler_entry* vmexit = vm_inst_->get_handlers()->v_handlers[MNEMONIC_VM_EXIT];
        const auto vmexit_address = vmexit->get_vm_handler_va(bit64);

        // mov VCSRET, ZLABEL(target)
        container.add(RECOMPILE(zydis_helper::enc(ZYDIS_MNEMONIC_MOV, ZREG(VCSRET), ZLABEL(target))));

        // lea VRIP, [VBASE + vmexit_address]
        container.add(RECOMPILE(zydis_helper::enc(ZYDIS_MNEMONIC_LEA, ZREG(VIP), ZMEMBD(VBASE, vmexit_address->get(), 8))));
        container.add(zydis_helper::enc(ZYDIS_MNEMONIC_JMP, ZREG(VIP)));
    }

    void vm_virtualizer::create_vm_jump(zyids_mnemonic mnemonic, asmbl::function_container& container, asmbl::code_label* rva_target)
    {
        asmbl::code_label* rel_label = asmbl::code_label::create("call_vm_enter_rel");
        container.add(rel_label, RECOMPILE(zydis_helper::enc(mnemonic, ZJMP(rva_target, rel_label))));
    }

    std::pair<bool, asmbl::function_container> vm_virtualizer::translate_to_virtual(const zydis_decode& decoded_instruction,
        uint64_t original_rva)
    {
        handle::inst_handler_entry* handler = vm_inst_->get_handlers()->inst_handlers[decoded_instruction.instruction.mnemonic];
        if (!handler)
            return {false, {}};

        return handler->translate_to_virtual(decoded_instruction, original_rva);
    }

    std::vector<uint8_t> vm_virtualizer::create_padding(const size_t bytes)
    {
        std::vector<uint8_t> padding;
        padding.reserve(bytes);

        for (int i = 0; i < bytes; i++)
            padding.push_back(util::ran_device::get().gen_8());

        return padding;
    }

    encoded_vec vm_virtualizer::create_jump(const uint32_t rva, asmbl::code_label* rva_target)
    {
        zydis_encoder_request jmp = zydis_helper::enc(ZYDIS_MNEMONIC_JMP, ZIMMS(rva_target->get() - rva - 5));
        return zydis_helper::encode_request(jmp);
    }
}
