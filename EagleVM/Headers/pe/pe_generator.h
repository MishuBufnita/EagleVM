#pragma once
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <Windows.h>

#include "pe/pe_sections/pe_code_section.h"
#include "pe/pe_sections/pe_handler_section.h"

constexpr auto default_image_base = 140000000;
constexpr auto default_section_alignment = 1000;
constexpr auto default_file_alignment = 200;

class pe_generator
{
public:
	PIMAGE_DOS_HEADER build_dos_header();
	PIMAGE_FILE_HEADER build_coff_header();
	PIMAGE_OPTIONAL_HEADER build_optional_header(uint64_t image_base, int section_alignment, int file_alignment, uint64_t sr, uint64_t sc, uint64_t hr, uint64_t hc);
	std::vector<IMAGE_SECTION_HEADER>* build_section_table(std::vector<PIMAGE_SECTION_HEADER> sections);

private:
	IMAGE_SECTION_HEADER vm_handler_section;

	IMAGE_DOS_HEADER dos_header{};
	IMAGE_FILE_HEADER coff_header{};
	IMAGE_OPTIONAL_HEADER optional_header{};
	std::vector<IMAGE_SECTION_HEADER> section_headers{};
};