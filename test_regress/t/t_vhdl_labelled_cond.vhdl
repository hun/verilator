-- DESCRIPTION: Verilator: VHDL a labelled generate condition
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An if-generate's condition may carry a label, `if name: condition generate`, as
-- poc's ocrom ROMs write it: `gen: if gInfer: (VENDOR = VENDOR_GENERIC) or ... generate`.
-- The label is a name and nothing else, and the expression after it is the condition --
-- the value each of GHDL and nvc simulates for these three branches.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_labelled_cond is
  port (
    a : out std_logic;
    b : out std_logic;
    c : out std_logic
  );
end entity;

architecture rtl of t_vhdl_labelled_cond is
begin
  -- The label `g1` does not decide: the condition is `true`
  g1: if false: true generate
    a <= '1';
  else generate
    a <= '0';
  end generate;

  g2: if true: false generate
    b <= '1';
  else generate
    b <= '0';
  end generate;

  -- No label: the condition is the expression
  g3: if true generate
    c <= '1';
  else generate
    c <= '0';
  end generate;
end architecture;