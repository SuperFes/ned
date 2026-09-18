# ~/.ssh/config has the basename "config", too generic to claim by name;
# the system-wide file is claimed, the per-user one needs a manual mode
# switch until path-pattern claims exist.
{:name "ssh_config"
 :extensions [".ssh_config"]
 :filenames ["ssh_config"]
 :line-comment "#"
}
