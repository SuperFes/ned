defmodule Calc do
  def total(items) do
    sum = first(items) +
      second(items)
    result =
      items
      |> Enum.map(&double/1)
      |> Enum.sum()
    items
    |> Enum.filter(&positive?/1)
    |> Enum.count()
  end
end
