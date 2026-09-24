defmodule Shop.Cart do
  @moduledoc "A cart."

  defstruct items: [], total: 0

  def add(%__MODULE__{} = cart, item) do
    %{cart | items: [item | cart.items]}
  end

  def checkout(cart) do
    try do
      charge(cart)
    rescue
      e in RuntimeError ->
        {:error, e}
    after
      log(cart)
    end
  end

  def label(n) do
    cond do
      n > 10 -> "many"
      true -> "few"
    end
  end

  defp log(cart), do: IO.inspect(cart)
end
