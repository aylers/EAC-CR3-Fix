#pragma once

auto read_physical(PVOID target_address,
	PVOID buffer,
	SIZE_T size,
	SIZE_T* bytes_read) -> NTSTATUS
{
	MM_COPY_ADDRESS to_read = { 0 };
	to_read.PhysicalAddress.QuadPart = (LONGLONG)target_address;
	return KM(MmCopyMemory)(buffer, to_read, size, MM_COPY_MEMORY_PHYSICAL, bytes_read);
}
NTSTATUS write_phyiscal(PVOID target_address,
	PVOID buffer,
	SIZE_T size,
	SIZE_T* bytes_read)
{
	if (!target_address) {
		return STATUS_UNSUCCESSFUL;
	}
	PHYSICAL_ADDRESS to_write = { 0 };
	to_write.QuadPart = LONGLONG(target_address);
	PVOID sp_mapped_memory = KM(MmMapIoSpaceEx)(to_write, size, PAGE_READWRITE);
	if (!sp_mapped_memory) {
		return STATUS_UNSUCCESSFUL;
	}
	KM(memcpy)(sp_mapped_memory, buffer, size);
	*bytes_read = size;
	KM(MmUnmapIoSpace)(sp_mapped_memory, size);
	return STATUS_SUCCESS;
}

namespace pml
{
	PVOID split_memory(PVOID SearchBase, SIZE_T SearchSize, const void* Pattern, SIZE_T PatternSize)
	{
		const UCHAR* searchBase = static_cast<const UCHAR*>(SearchBase);
		const UCHAR* pattern = static_cast<const UCHAR*>(Pattern);

		for (SIZE_T i = 0; i <= SearchSize - PatternSize; ++i) {
			SIZE_T j = 0;
			for (; j < PatternSize; ++j) {
				if (searchBase[i + j] != pattern[j])
					break;
			}
			if (j == PatternSize)
				return const_cast<UCHAR*>(&searchBase[i]);
		}

		return nullptr;
	}

	void* g_mmonp_MmPfnDatabase;

	static NTSTATUS InitializeMmPfnDatabase()
	{
		struct MmPfnDatabaseSearchPattern
		{
			const UCHAR* bytes;
			SIZE_T bytes_size;
			bool hard_coded;
		};

		MmPfnDatabaseSearchPattern patterns;

		// Windows 10 x64 Build 14332+
		static const UCHAR kPatternWin10x64[] = {
			0x48, 0x8B, 0xC1,        // mov     rax, rcx
			0x48, 0xC1, 0xE8, 0x0C,  // shr     rax, 0Ch
			0x48, 0x8D, 0x14, 0x40,  // lea     rdx, [rax + rax * 2]
			0x48, 0x03, 0xD2,        // add     rdx, rdx
			0x48, 0xB8,              // mov     rax, 0FFFFFA8000000008h
		};

		patterns.bytes = kPatternWin10x64;
		patterns.bytes_size = sizeof(kPatternWin10x64);
		patterns.hard_coded = true;
		const auto p_MmGetVirtualForPhysical = reinterpret_cast<UCHAR*>(((KM(MmGetVirtualForPhysical))));
		if (!p_MmGetVirtualForPhysical) {

			return STATUS_PROCEDURE_NOT_FOUND;
		}

		auto found = reinterpret_cast<UCHAR*>(split_memory(p_MmGetVirtualForPhysical, 0x20, patterns.bytes, patterns.bytes_size));
		if (!found) {
			return STATUS_UNSUCCESSFUL;
		}


		found += patterns.bytes_size;
		if (patterns.hard_coded) {
			g_mmonp_MmPfnDatabase = *reinterpret_cast<void**>(found);
		}
		else {
			const auto mmpfn_address = *reinterpret_cast<ULONG_PTR*>(found);
			g_mmonp_MmPfnDatabase = *reinterpret_cast<void**>(mmpfn_address);
		}

		g_mmonp_MmPfnDatabase = PAGE_ALIGN(g_mmonp_MmPfnDatabase);

		return STATUS_SUCCESS;
	}

	uintptr_t dirbase_from_base_address(void* base)
	{
		if (!NT_SUCCESS(InitializeMmPfnDatabase()))
			return 0;

		virt_addr_t virt_base{}; virt_base.value = base;

		size_t read{};

		auto ranges = KM(MmGetPhysicalMemoryRanges)();

		for (int i = 0;; i++) {

			auto elem = &ranges[i];

			if (!elem->BaseAddress.QuadPart || !elem->NumberOfBytes.QuadPart)
				break;

			UINT64 current_phys_address = elem->BaseAddress.QuadPart;

			for (int j = 0; j < (elem->NumberOfBytes.QuadPart / 0x1000); j++, current_phys_address += 0x1000) {

				_MMPFN* pnfinfo = (_MMPFN*)((uintptr_t)g_mmonp_MmPfnDatabase + (current_phys_address >> 12) * sizeof(_MMPFN));

				if (pnfinfo->u4.PteFrame == (current_phys_address >> 12)) {
					MMPTE pml4e{};
					if (!NT_SUCCESS(read_physical(PVOID(current_phys_address + 8 * virt_base.pml4_index), &pml4e, 8, &read)))
						continue;

					if (!pml4e.u.Hard.Valid)
						continue;

					MMPTE pdpte{};
					if (!NT_SUCCESS(read_physical(PVOID((pml4e.u.Hard.PageFrameNumber << 12) + 8 * virt_base.pdpt_index), &pdpte, 8, &read)))
						continue;

					if (!pdpte.u.Hard.Valid)
						continue;

					MMPTE pde{};
					if (!NT_SUCCESS(read_physical(PVOID((pdpte.u.Hard.PageFrameNumber << 12) + 8 * virt_base.pd_index), &pde, 8, &read)))
						continue;

					if (!pde.u.Hard.Valid)
						continue;

					MMPTE pte{};
					if (!NT_SUCCESS(read_physical(PVOID((pde.u.Hard.PageFrameNumber << 12) + 8 * virt_base.pt_index), &pte, 8, &read)))
						continue;

					if (!pte.u.Hard.Valid)
						continue;

					return current_phys_address;
				}
			}
		}

		return 0;
	}

}
auto translate_linear(
	UINT64 directory_base,
	UINT64 address) -> UINT64 {
	directory_base &= ~0xf;

	auto virt_addr = address & ~(~0ul << 12);
	auto pte = ((address >> 12) & (0x1ffll));
	auto pt = ((address >> 21) & (0x1ffll));
	auto pd = ((address >> 30) & (0x1ffll));
	auto pdp = ((address >> 39) & (0x1ffll));
	auto p_mask = ((~0xfull << 8) & 0xfffffffffull);
	size_t readsize = 0;
	UINT64 pdpe = 0;
	read_physical(PVOID(directory_base + 8 * pdp), &pdpe, sizeof(pdpe), &readsize);
	if (~pdpe & 1) {
		return 0;
	}
	UINT64 pde = 0;
	read_physical(PVOID((pdpe & p_mask) + 8 * pd), &pde, sizeof(pde), &readsize);
	if (~pde & 1) {
		return 0;
	}
	if (pde & 0x80)
		return (pde & (~0ull << 42 >> 12)) + (address & ~(~0ull << 30));
	UINT64 pteAddr = 0;
	read_physical(PVOID((pde & p_mask) + 8 * pt), &pteAddr, sizeof(pteAddr), &readsize);
	if (~pteAddr & 1) {
		return 0;
	}
	if (pteAddr & 0x80) {
		return (pteAddr & p_mask) + (address & ~(~0ull << 21));
	}
	address = 0;
	read_physical(PVOID((pteAddr & p_mask) + 8 * pte), &address, sizeof(address), &readsize);
	address &= p_mask;
	if (!address) {
		return 0;
	}
	return address + virt_addr;
}

NTSTATUS FixDTB(dtbl gja) {
	if (!gja->process_id) {
		return STATUS_UNSUCCESSFUL;
	}

	PEPROCESS procc = nullptr;
	KM(PsLookupProcessByProcessId)((HANDLE)gja->process_id, &procc);
	if (!procc) {
		return STATUS_UNSUCCESSFUL;
	}

	m_stored_dtb = pml::dirbase_from_base_address((void*)PsGetProcessSectionBaseAddress(procc));
	gja->base_address = pml::dirbase_from_base_address((void*)PsGetProcessSectionBaseAddress(procc));
	ObDereferenceObject(procc);

	ULONGLONG raaa = 1;
	RtlCopyMemory(gja->operation, &raaa, sizeof(raaa));
	return STATUS_SUCCESS;
}

NTSTATUS TranslateLinearAddress(UINT64 directoryBase, PVOID virtualAddress, UINT64* physicalAddress) {
	*physicalAddress = translate_linear(directoryBase, (UINT64)virtualAddress);
	return (*physicalAddress != 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
