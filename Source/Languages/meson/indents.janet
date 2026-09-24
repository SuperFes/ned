# ned-authored. Each branch of an if chain and a foreach body: the lines after
# their own first row. `endif` closes the chain outside every branch.
[(if_command) (elseif_command) (else_command) (foreach_command)] @indent.headed
(foreach_command "endforeach" @dedent)
