# Handwritten backpropagation (Task F)

Handwritten derivation by Arnav Mehta (two pages, photographed):

- `page1_forward_backward.jpg` — forward pass and chain-rule derivatives
- `page2_update_direction.jpg` — simultaneous update (η = 0.1) and gradient direction

The derivation contains:

1. the forward pass;
2. the chain-rule derivatives for w, b, v and c;
3. one simultaneous gradient-descent update using the gradients of the original parameters;
4. the numerical values;
5. a short explanation of the gradient direction.

Values checked against (computed by the C++ engine, `results/backprop_gradients.csv`):

| Quantity | Value |
|---|---|
| z = wx + b | 0.5 |
| h = tanh z | 0.46211716 |
| ŷ = vh + c | 0.46211716 |
| J = ½(ŷ − y)² | 1.18254182 |
| δ = ŷ − y | −1.53788284 |
| 1 − h² | 0.78644773 |
| ∂J/∂w = ∂J/∂b = δ v (1 − h²) x | −1.20946448 |
| ∂J/∂v = δ h | −0.71068205 |
| ∂J/∂c = δ | −1.53788284 |
| w, b, v, c after one step (η = 0.1) | 0.62094645, 0.12094645, 1.07106820, 0.15378828 |
| J after one step | 0.68577377 |
