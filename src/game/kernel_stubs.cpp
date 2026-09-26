// Kernel and XAM imports that Skylanders Giants needs in order to boot and that the
// ReXGlue runtime does not implement. Every entry records why it is safe to stub.

#include <rex/hook.h>

// Entries are added one at a time as the boot log reports them, for example:
//   REX_EXPORT_STUB_RETURN(ExampleImport, 0)  // returns STATUS_SUCCESS; result unused by game
