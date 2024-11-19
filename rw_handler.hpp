#pragma once

LONG64 find_min(INT32 g,
	SIZE_T f)
{
	INT32 h = (INT32)f;
	ULONG64 result = 0;

	result = (((g) < (h)) ? (g) : (h));

	return result;
}

NTSTATUS ReadWriteHandler(prw x)
{
	if (!x->process_id) {
		return STATUS_UNSUCCESSFUL;
	}
	PEPROCESS PROCCSS = NULL;
	PsLookupProcessByProcessId((HANDLE)(x->process_id), &PROCCSS);
	if (!PROCCSS) {
		return STATUS_UNSUCCESSFUL;
	}
	INT64 physicaladdress;
	physicaladdress = translate_linear(m_stored_dtb, (ULONG64)(x->address));
	if (!physicaladdress) {
		return STATUS_UNSUCCESSFUL;
	}
	ULONG64 finalsize = find_min(PAGE_SIZE - (physicaladdress & 0xFFF), x->size);
	SIZE_T bytestrough = NULL;
	if (x->write) {
		write_phyiscal(PVOID(physicaladdress), (PVOID)((ULONG64)(x->buffer)), finalsize, &bytestrough);
	}
	else
	{
		read_physical(PVOID(physicaladdress), (PVOID)((ULONG64)(x->buffer)), finalsize, &bytestrough);
	}
	return STATUS_SUCCESS;
}
