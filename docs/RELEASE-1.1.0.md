## Force Link Audio 1.1.0 — OpenMPC add-in

Publish the Force's stereo main mix as a Link Audio channel while keeping the
Force standalone. By Mac Digi / Rare Data, originally built for the COMPA project.

This release adds the OpenMPC catalog package and shared installer. The audio tap
stays small; the Link Audio publisher remains a separate process and now exits
when its MPC parent exits. The build targets armhf/glibc 2.31; other MPC hardware
is unverified.

**Network audio is off by default.** Read README-addin.txt, disable the old
MockbaMod ForceLinkAudio addon if present, then install the new package. Set
`enabled=1` in the installed settings only when you want to publish audio on your
trusted LAN. Save your project before the installer restarts MPC.

Original legacy releases remain available. This package uses the unmodified
OpenMPC shared installer and preserves other preload entries. See INSTALL.md for
its changes and uninstall procedure, including the shared boot-reload integration.

Source: the matching tag in this repository. GPL-2.0-or-later; third-party notices
are included. Thanks to sd88me/OpenMPC, MockbaMod contributors and Amit Talwar.

### Verification

Catalog validation, 12 config-parser cases, 77 shared-installer checks and the
isolated ARM lifecycle test passed. Real Force installation, settings preservation and rollback passed. The owner
confirmed normal local sound and controls while looping a saved project. A Mac
Link Audio receiver captured 641,000 non-silent stereo frames with no clipping.
Other MPC models remain unverified.
