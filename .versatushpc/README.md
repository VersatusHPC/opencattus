# VersatusHPC CI/CD Contract

This directory is the OpenCATTUS project-side contract for the VersatusHPC
CI/CD platform (`VersatusHPC/ci-cd-platform`).

- `Jenkinsfile` is intentionally minimal and only calls the shared library.
- `project.yaml` declares the build matrix, cache mounts, change policy, and
  publish intent. The thin build adapters live in `ci/` (`ci/build.sh`,
  `ci/preflight.sh`, `ci/setup-<distro>.sh`) and are referenced from
  `project.yaml`; the integration-lab lane configs live in `ci/lab/`.

OpenCATTUS does not call Pulp directly, sign packages, or store
Jenkins/Pulp/signing credentials. Publishing, signing, and release promotion
are owned by the `ci-cd-platform` shared library.

## Project-specific policy

- Packages are built with CPack from the CMake tree: binary RPMs to
  `out/rpm/<distro>/<arch>/` and DEBs to `out/deb/<distro>/<dpkg arch>/`.
  No SRPMs and no debug packages are produced (debug packages are disabled
  in `cmake/CPackProperties.cmake`); there is therefore no
  testing-debug/release-debug content for this project.
- The package version always equals the full CMake `PROJECT_VERSION`,
  including `.0` patch releases.
- Release tags are plain `v<VERSION>` (for example `v1.2.0`): the pipeline's
  classify stage pins the tag to the CMake project version, and the platform
  manifest accepts the plain-version form (ci-cd-platform#23). This differs
  from packaging projects such as cde, which tag `v<VERSION>-<RELEASE>`.
- Testing publishes from trusted `master` with install smoke on EL8/EL9/EL10
  and Ubuntu 24.04. Tag builds additionally run the full libvirt integration
  lab (six lanes under `ci/lab/`, each with lane-isolated subnets) and then
  submit the promotion manifest to `promote-rpm-release`.
- Release managers (promotion approvers): ferrao, daniel, dieguez
  @versatushpc.com.br.
- Pull requests run preflight only; the container build matrix runs on
  `master` and tags.
