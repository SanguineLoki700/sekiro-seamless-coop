#pragma once

#include "skcoop/save_policy.hpp"

namespace skcoop::saves {

enum class NativePathAction { passthrough, redirect, deny };
enum class NativePathError {
    none, invalid_binding, policy_rejected, ambiguous_save_path,
    account_mismatch, mixed_namespaces, same_destination,
};

struct NativePathDecision {
    NativePathAction action{NativePathAction::deny};
    NativePathError error{NativePathError::invalid_binding};
    SavePolicyError policy_error{SavePolicyError::none};
    SaveFileKind kind{SaveFileKind::primary};
    std::wstring account_id;
    // Populated only for redirect, including an already redirected input.
    // deny and passthrough never provide a usable save destination.
    std::wstring destination;
};

struct NativePathPairDecision {
    NativePathAction action{NativePathAction::deny};
    NativePathError error{NativePathError::invalid_binding};
    NativePathDecision source;
    NativePathDecision target;
};

// Pure lexical file-path classification, not authentication, a hook or a
// filesystem sandbox. The caller establishes context facts and physically
// validates destinations/aliases at the I/O boundary. Directory queries and
// enumeration require a separate adapter; this API does not virtualize them.
//
// One explicitly bound account is fixed for the operation. Exact solo names
// below the declared solo tree or cooperative AppData tree redirect to that
// account's cooperative extension (the latter supports a private AppData query);
// exact cooperative names already there redirect idempotently. Unsupported
// save-like paths in either tree and ambiguous relative save names are denied.
// Canonical S0000 save basenames outside both trees are denied too: an engine
// can absolutize its '.' fallback before calling Win32. Different unrelated
// filenames outside the trees remain passthrough, not claimed as cooperative.
// A denial must NEVER be retried against the original solo path.
[[nodiscard]] NativePathDecision decide_native_save_path(
    std::wstring_view path, const SavePolicyConfig&, const SaveProcessContext&,
    std::wstring_view bound_account);

// Both sides must be unrelated, or both must resolve to different files in the
// same bound cooperative account/tree. Mixed unrelated/save operations fail.
// On any denial both child destinations/accounts are cleared, including a side
// that was individually accepted. No I/O may precede the complete decision.
[[nodiscard]] NativePathPairDecision decide_native_save_pair(
    std::wstring_view source, std::wstring_view target,
    const SavePolicyConfig&, const SaveProcessContext&,
    std::wstring_view bound_account);

} // namespace skcoop::saves
