# ~/.gitconfig, .gitmodules, a repository's .git/config and the XDG
# ~/.config/git/config share the syntax.
{:name "gitconfig"
 :extensions [".gitconfig"]
 :filenames [".gitconfig" ".gitmodules" "gitconfig" ".git/config" "git/config"]
 :line-comment "#"
 :import-resolution {:home-prefix true}
}
