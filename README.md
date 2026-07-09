# 🧭 CMP-OffsetPorter

**CMP-OffsetPorter** is a native **Win32 / C++17** utility for porting PlayStation cheat-code addresses from one region/version to another by applying address offsets.

It is designed for CMP-style code database work where large code blocks need to be pasted, converted, reviewed, and exported quickly.

---

## ✨ Quick Summary

| Item | Details |
|---|---|
| 🖥️ App Type | Native Win32 desktop GUI |
| ⚙️ Language | C++17 |
| 🪟 Target | Windows 7-compatible API target |
| 🎮 Code Support | PlayStation 1 and PlayStation 2 code-type-aware offsetting |
| 📋 Main Use | Port cheat codes between regions/versions using known address offsets |
| 🧾 Output | Preview text, normal export, CSV/grid export, and G-Sheets layouts |
| 🛠️ Build | One root build command: `0-BUILD-WINDOWS7-x64.cmd` |

---

<details open>
<summary>🚀 What This Project Does</summary>

CMP-OffsetPorter takes a base-region code list and applies a calculated offset for selected target regions.

Example use case:

- You have a USA code list.
- You know the PAL address difference/offset.
- You paste the USA codes into **Base-Region Codes**.
- You select the base region and target region.
- CMP-OffsetPorter generates the ported target-region codes.

The program is built to handle very large pasted code blocks and keep CMP-style formatting, names, credits, comments, and code groups intact where possible.

</details>

---

<details open>
<summary>🎮 Code-Type-Aware Porting</summary>

CMP-OffsetPorter includes a **Code Type** button that cycles between:

```text
Off → PS1 → PS2 → Off
```

### 🔴 Off

Uses the older/simple behavior:

- Finds the first 8-hex token on a line.
- Applies the selected offset to that token.

This is useful for plain address lists or custom formats where code-type parsing is not wanted.

### 🟦 PS1

Uses PlayStation 1-aware handling so common control/data rows are not blindly treated as normal addresses.

### 🟩 PS2

Uses PlayStation 2-aware handling so only real address fields are ported.

For example:

```text
+All Weapons
%Credits: Code Master
$00445A99 00000009
$10445A9A 00000909
$40445A9C 00190001
$09090909 00000000
$00445B00 00000009
```

In PS2 mode, the `4` serial-write code is handled correctly:

```text
$40445A9C 00190001   ← address/control row gets ported
$09090909 00000000   ← data/value row stays unchanged
```

That prevents payload rows from being accidentally shifted as if they were addresses.

</details>

---

<details>
<summary>🧩 Main Features</summary>

- 🧭 Base-region to target-region offset conversion.
- 🎮 PS1 / PS2 code-type-aware conversion mode.
- 📋 Large paste support in the **Base-Region Codes** section.
- 🧾 Preserves normal CMP-style code text such as names and credits.
- 📊 CSV/grid export support.
- 🟩 G-Sheets mode and G-Sheets layout options.
- 🪟 Native Win32 GUI with no .NET runtime requirement.
- 🛠️ Single visible build CMD in the project root.
- 📁 Build output and logs are written under `0-Finished`.

</details>

---

<details>
<summary>🖱️ Basic Workflow</summary>

1. Open `OffsetPorter.exe`.
2. Select the **Base Region**.
3. Paste the original/base-region codes into **Base-Region Codes**.
4. Choose the target region/output options.
5. Set **Code Type** to:
   - `Off` for simple first-token offsetting.
   - `PS1` for PlayStation 1 code handling.
   - `PS2` for PlayStation 2 code handling.
6. Review the generated output.
7. Export as text, CSV, or G-Sheets-friendly output as needed.

</details>

---

<details>
<summary>🧰 Build Requirements</summary>

To build the native C++ version, Visual Studio must include the C++ tools.

Required:

- Visual Studio 2026, 2022, 2019, or compatible Build Tools.
- **Desktop development with C++** workload.
- MSVC x64/x86 build tools.
- CMake tools for Windows.

A Visual Basic-only install cannot build this project because it does not include the native C++ compiler tools such as `cl.exe` and `nmake.exe`.

</details>

---

<details>
<summary>🪟 Windows 7 Compatibility Notes</summary>

The project is written as a native Win32 application and targets Windows 7-compatible APIs through compile definitions:

```text
WINVER=0x0601
_WIN32_WINNT=0x0601
```

The build does **not** force the old Windows 7 SDK. This allows newer Visual Studio versions and newer Windows SDKs to configure and build the project while still avoiding newer API calls in the source.

</details>
<details>
<summary>📝 Version Notes</summary>

### v1.00.8

- Reduced root build commands down to one visible CMD:

```text
0-BUILD-WINDOWS7-x64.cmd
```

- Kept the PowerShell build helper inside `tools\`.
- Preserved Visual Studio 2026 / 2022 / 2019 / 2017 fallback support.
- Preserved NMake fallback support.

### Earlier C++17 Port Notes

- Converted from the original .NET/WinForms-style project into native Win32/C++17.
- Added Windows 7-compatible API targeting.
- Added PS1 / PS2 code-type-aware conversion mode.
- Added large paste support for the Base-Region Codes input.
- Added build fixes for VS2026, manifest duplication, and runtime flag conflicts.

</details>
