# Zephyr T2 Topology Workspace Setup: OpenScale

This document describes the **T2 (App-centric) Topology** workspace setup used for the OpenScale project.

---

## 1. What is a T2 Topology?

In Zephyr's `west` build tool ecosystem, workspaces can be organized in different topologies. 

```
               [ Workspace Root: OpenScale/ ]
                             │
       ┌─────────────────────┼─────────────────────┐
       │                     │                     │
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│  .west/      │      │  OpenScale/  │      │   zephyr/    │
│  (Config)    │      │  (App/Manifest)     │  (RTOS Kernel)│
└──────────────┘      └──────────────┘      └──────────────┘
                             │
                       ┌─────┴─────┐
                       ▼           ▼
                   west.yml    app/ src/
```

Under a **T2 (App-centric) Topology**:
1. **The Application is the Manifest Repository**: The repository containing your application code (`OpenScale/OpenScale/`) also contains the `west.yml` manifest file that describes all software dependencies (Zephyr RTOS, Nordic Connect SDK, custom drivers, libraries).
2. **Self-Location**: The application repository is checked out *inside* the workspace directory, and its location is defined by the `manifest.self.path` option (set to `OpenScale` in this setup).
3. **Workspace Isolation**: All dependencies are cloned relative to the top-level workspace root (`OpenScale/`), keeping the workspace clean and isolated.

---

## 2. Directory Layout & Architecture

The workspace is structured as follows:

```
/Users/elvis/Developer/github.com/elvishuynh/OpenScale/
├── .west/                       # West configuration directory
│   └── config                   # Points West to the manifest repo and file
├── OpenScale/                   # Main Application & Manifest Repository (This repo)
│   ├── app/                     # Application source code
│   ├── doc/                     # Documentation files (like this one)
│   ├── zephyr/                  # Application board configurations and overlays
│   └── west.yml                 # West Manifest file defining all dependencies
├── bootloader/                  # MCUboot bootloader (imported via sdk-nrf)
├── modules/                     # Zephyr modules (HALs, filesystems, etc. imported via sdk-nrf)
├── nrf/                         # Nordic Connect SDK (NCS) core repository
├── nrfxlib/                     # Nordic proprietary helper libraries (imported via sdk-nrf)
├── zephyr/                      # Upstream Zephyr RTOS repository (imported via sdk-nrf)
├── zephyr-tm1640-driver/        # Custom TM1640 display driver repository
├── pt18-led-matrix-module/      # Custom PT18 LED Matrix driver module repository
└── tools/                       # Tool repositories (imported via sdk-nrf)
```

---

## 3. Configuration Analysis

### A. West Local Configuration (`.west/config`)
Located at the top level of the workspace, the `.west/config` file instructs `west` where to locate the manifest:

```ini
[manifest]
path = OpenScale
file = west.yml

[zephyr]
base = zephyr
```

* **`manifest.path`**: Tells `west` that the repository acting as the manifest repository is located at `OpenScale` (relative to the workspace root).
* **`manifest.file`**: Tells `west` that the manifest filename is `west.yml`.

---

### B. West Manifest File (`OpenScale/west.yml`)
Located at the root of your application repository, `west.yml` manages all dependencies. Below is the active configuration:

```yaml
manifest:
  remotes:
    - name: ncs
      url-base: https://github.com/nrfconnect
    - name: elvishuynh
      url-base: https://github.com/elvishuynh

  projects:
    - name: nrf
      repo-path: sdk-nrf
      remote: ncs
      revision: v3.3.1
      import: true

    - name: zephyr-tm1640-driver
      remote: elvishuynh
      revision: main

    - name: pt18-led-matrix-module
      remote: elvishuynh
      revision: main

  self:
    path: OpenScale
```

#### Key Elements of `west.yml`:
* **`remotes`**: Declares upstream base URLs (`ncs` for Nordic Connect SDK, and `elvishuynh` for custom repositories).
* **`projects`**:
  * **`nrf`**: Fetches the core Nordic Connect SDK repository (`sdk-nrf`) at revision `v3.3.1`. 
    * `import: true`: This is a **sub-manifest import**. It tells `west` to read `nrf/west.yml` and import all of its defined repositories (like `zephyr`, `bootloader/mcuboot`, `nrfxlib`, etc.) directly into this workspace.
  * **`zephyr-tm1640-driver`**: Fetches your custom TM1640 display driver from your GitHub account.
  * **`pt18-led-matrix-module`**: Fetches your custom PT18 LED matrix module from your GitHub account.
* **`self`**:
  * `path: OpenScale`: Tells `west` that when it runs commands, the local repository containing this `west.yml` is located at the path `OpenScale` within the workspace.

---

## 4. How to Manage and Sync this Workspace

Because this is a T2 topology workspace, you can manage all dependencies easily using standard `west` command-line tools.

### Cloning & Recreating the Workspace
To recreate this workspace setup on a new machine:

1. **Initialize the workspace** pointing to your main application repo:
   ```bash
   west init -m https://github.com/elvishuynh/OpenScale --mr main OpenScale-Workspace
   ```
   *This clones the application repo into `OpenScale-Workspace/OpenScale` and creates the `.west/config` file pointing to it.*

2. **Change directory** into the workspace:
   ```bash
   cd OpenScale-Workspace
   ```

3. **Fetch all dependencies** defined in the manifest (including the imported NCS dependencies):
   ```bash
   west update
   ```

### Everyday Commands
* **Check status of all repos**:
  ```bash
  west status
  ```
* **Verify configuration details**:
  ```bash
  west config -l
  ```
* **Fetch the latest versions of dependencies**:
  ```bash
  west update
  ```
