Set sh = CreateObject("WScript.Shell")
desktop = sh.SpecialFolders("Desktop")
Set lnk = sh.CreateShortcut(desktop & "\Build Editor.lnk")
lnk.TargetPath = "C:\Windows\System32\cmd.exe"
lnk.Arguments = "/c ""C:\Tools\MaterialLab\MatLab\BuildEditor.bat"""
lnk.WorkingDirectory = "C:\Tools\MaterialLab\MatLab"
lnk.WindowStyle = 1
lnk.Description = "Build MaterialLab Editor"
lnk.IconLocation = "C:\Windows\System32\shell32.dll,165"
lnk.Save
WScript.Echo "Created Desktop\Build Editor.lnk" & vbCrLf & "Right-click it -> Pin to taskbar"
