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
rollback, and reinstall of the new adapter. The owner confirmed local sound and
controls were normal while playing. The diagnostic Mac receiver captured silence; the performance may have ended
before the capture began. A direct localhost packet
check also measured silence on all four S32 channels, so non-silent end-to-end
capture remains pending. Do not claim it passed yet.

After release and PR creation, reply to sd88me in OpenMPC #showcase with the
release and PR links and the actual tested scope. Do not claim catalog acceptance
before the maintainers merge it. Other MPC hardware remains unverified.
