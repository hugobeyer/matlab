Set sh = CreateObject("WScript.Shell")
desktop = sh.SpecialFolders("Desktop")
Set lnk = sh.CreateShortcut(desktop & "\MatLab.lnk")
lnk.TargetPath = "C:\Windows\System32\cmd.exe"
lnk.Arguments = "/c start """" ""C:\Tools\MaterialLab\MatLab\MatLab.uproject"""
lnk.WorkingDirectory = "C:\Tools\MaterialLab\MatLab"
lnk.WindowStyle = 7
lnk.Description = "Open MatLab Unreal project"
lnk.IconLocation = "C:\Windows\System32\shell32.dll,3"
lnk.Save
WScript.Echo "Created Desktop\MatLab.lnk" & vbCrLf & "Right-click it -> Pin to taskbar"
