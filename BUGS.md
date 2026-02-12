# Known Issues

## Generate block support: `in_autologic_block` state leak

`processMemberRecursive()` passes `in_autologic_block` by reference through the
recursion. If a `BEGIN_AUTOLOGIC` marker were inside a generate block, the state
would bleed into sibling members or other branches of an if/else generate. This
doesn't cause problems today (AUTOLOGIC is always at the top level), but is a
latent bug. Fix: either scope the flag per-recursion or assert that AUTOLOGIC
markers aren't found inside generate constructs.

## Generate block support: missing test fixtures

No integration tests exercise AUTOINST inside generate constructs (if generate,
case generate, for generate, nested generates). Add fixtures to
`tests/fixtures/` and corresponding test cases in `test_integration.cpp`.
