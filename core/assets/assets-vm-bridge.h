#ifndef ASSETS_VM_BRIDGE_H
#define ASSETS_VM_BRIDGE_H

#include "assets-error.h"
#include "../ruby-vm/ruby-vm-error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert AssetsError to RubyVMError
 *
 * Maps asset-specific error codes to corresponding VM error codes
 * and transfers error context/message.
 *
 * @param assets_error Source assets error (can be NULL if only code is known)
 * @param assets_code Assets error code (used if assets_error is NULL)
 * @param vm_error Destination VM error structure
 * @return Corresponding RubyVMErrorCode
 */
RubyVMErrorCode assets_error_to_vm_error(const AssetsError* assets_error,
                                          AssetsErrorCode assets_code,
                                          RubyVMError* vm_error);

/**
 * Get the corresponding VM error code for an assets error code
 *
 * @param assets_code Assets error code
 * @return Corresponding RubyVMErrorCode
 */
RubyVMErrorCode assets_code_to_vm_code(AssetsErrorCode assets_code);

#ifdef __cplusplus
}
#endif

#endif // ASSETS_VM_BRIDGE_H
