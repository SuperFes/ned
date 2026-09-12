import { useState } from "react";

interface Props {
    items: string[];
}

export function Panel(props: Props) {
    const [open, setOpen] = useState(false);

    const rows = props.items.map((item) => (
        <li className="row" key={item}>
            {item}
        </li>
    ));

    return (
        <section
            className="panel"
            onClick={() => setOpen(!open)}
        >
            <ul>{rows}</ul>
        </section>
    );
}
