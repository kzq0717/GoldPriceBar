param (
    [string]$Title = "Notification",
    [string]$Message = "Task finished",
    [string]$AppId = "AutomationAgent"
)

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null

$safeTitle = [System.Security.SecurityElement]::Escape($Title)
$safeMsg = [System.Security.SecurityElement]::Escape($Message)

$xmlText = @"
<toast duration="long">
    <visual>
        <binding template="ToastGeneric">
            <text>$safeTitle</text>
            <text>$safeMsg</text>
        </binding>
    </visual>
    <audio src="ms-winsoundevent:Notification.Default" />
</toast>
"@

$xml = New-Object Windows.Data.Xml.Dom.XmlDocument
$xml.LoadXml($xmlText)
$toast = [Windows.UI.Notifications.ToastNotification]::new($xml)
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($AppId).Show($toast)
Write-Output "SUCCESS"
