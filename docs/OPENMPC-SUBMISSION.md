# Prepared catalog submission

Submit one registry entry at `catalog/plugins/force-link-audio.json` to
`sd88me/mpc-vst-plugins` after the candidate's musical acceptance passes and
its GitHub release is published. Use the original `macdigi/force-link-audio`
repository so authorship and the COMPA origins remain clear.

The package uses the unmodified shared add-in installer pinned to
`e18a0a1da84515096d354f399dd4cd7f9c079fb5`. It targets armhf, publishes the stereo
main mix as Link Audio, and is disabled until the owner enables it. No HTTP
control surface is exposed. Explain the validator's two manual-review flags:
network sockets are for localhost capture and Link Audio; posix_spawn launches
the separate publisher, with no shell evaluation and no inherited LD_PRELOAD.

Completed real-device checks: default disabled behavior, enable/startup,
settings-preserving reinstall, other preloads retained, uninstall, original-addon
rollback, and reinstall of the new adapter. Owner confirmed normal local sound
and controls. Final continuous-playback Mac capture: 641,000 stereo frames,
1,281,616 nonzero samples, zero clipped samples. See OPENMPC-VERIFICATION.md.

After release and PR creation, reply to sd88me in OpenMPC #showcase with the
release and PR links and the actual tested scope. Do not claim catalog acceptance
before the maintainers merge it. Other MPC hardware remains unverified.

## Submission and review follow-up (October 14, 2026)

Release v1.1.0 is public. Catalog PR:
https://github.com/sd88me/mpc-vst-plugins/pull/438

The requested network/runs-programs categories are proposed for maintainer
confirmation. The summary warns to disable the legacy addon. The optional
2:1 signal-flow image is attached to v1.1.0 without moving its tag or changing
the tested ZIP. All three packaged shared-installer files were compared byte
for byte with the intentional e18a0a1 pin and matched. Registry validation
passed for 119 entries; ZIP validation retained only the two expected warnings.
The PR body now includes “no docs needed” for the catalog repository.
