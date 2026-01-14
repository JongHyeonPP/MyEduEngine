---
description: Build and deploy DORO Web Editor to test folder
---

// turbo-all

# DORO Web Editor Build & Deploy

## 1. Build the Web Editor
```powershell
cd c:\Users\USER\Documents\godot
scons platform=web target=editor -j4
```

## 2. Copy ALL required files to web_editor_test
After build completes, copy files with correct naming:
```powershell
cd c:\Users\USER\Documents\godot
# CRITICAL: Use wrapped.js for Engine + Godot combined, NOT the plain .js file
Copy-Item -Path "bin\godot.web.editor.wasm32.wasm" -Destination "bin\web_editor_test\godot.editor.wasm" -Force
Copy-Item -Path "bin\godot.web.editor.wasm32.wrapped.js" -Destination "bin\web_editor_test\godot.editor.js" -Force
Write-Host "All files copied! $(Get-Date -Format 'HH:mm:ss')"
```

## 3. Verify files are updated
```powershell
Get-Item "bin\web_editor_test\godot.editor.wasm", "bin\web_editor_test\godot.editor.js" | ForEach-Object { "$($_.Name): $($_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" }
```

## 4. Hard reload browser
In the browser, press Ctrl+Shift+R to force reload without cache.

---

## Important Notes
- **index.html loads `godot.editor.js`** which must be the wrapped.js (Engine + Godot combined)
- **WRONG**: `godot.web.editor.wasm32.js` (291KB) - Godot only, no Engine class
- **CORRECT**: `godot.web.editor.wasm32.wrapped.js` (319KB) - Engine + Godot combined
- Build output is `godot.web.editor.wasm32.*`
- Test folder expects files named `godot.editor.*`
