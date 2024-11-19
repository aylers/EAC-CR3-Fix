typedef struct _dtb {
	INT32 process_id;
	bool* operation;
	UINT64 base_address;
} dtb, * dtbl;

typedef struct _rw {
	INT32 process_id;
	ULONGLONG address;
	ULONGLONG buffer;
	ULONGLONG size;
	BOOLEAN write;
} rw, * prw;
