SELECT a.id,
    a.name,
    CASE
        WHEN a.kind = 1 THEN 'one'
        ELSE 'other'
    END AS kind
FROM accounts a
JOIN owners o
    ON o.id = a.owner_id
LEFT JOIN regions r ON r.id = a.region_id
WHERE a.active
    AND a.id IN (
        SELECT account_id
        FROM payments
        WHERE amount > 100
    )
GROUP BY a.id,
    a.name
ORDER BY a.name;

SELECT
    count(*)
FROM accounts;

WITH recent AS (
    SELECT id
    FROM accounts
    WHERE created > now()
)
SELECT id
FROM recent;

INSERT INTO accounts (id, name)
VALUES
    (1, 'a'),
    (2, 'b');

UPDATE accounts
SET name = 'x',
    active = false
WHERE id = 1;

DELETE FROM accounts
WHERE id = 2;

CREATE TABLE owners (
    id integer PRIMARY KEY,
    name text NOT NULL
);

INSERT INTO owners (id, name)
VALUES
    (1, 'a'),
    (2, 'b')
ON CONFLICT (id) DO NOTHING;

UPDATE accounts
SET owner_id = o.id
FROM owners o
WHERE o.name = accounts.name;
