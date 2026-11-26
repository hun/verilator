-- DESCRIPTION: Verilator: a nested aggregate assignment target is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An aggregate target whose element is itself an aggregate is not lowered.
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, b : in bit;
    y : out bit_vector(0 to 1)
  );
end entity t;

architecture rtl of t is

begin
  process (a, b)
    variable x, z : bit;
    variable w : bit;
  begin
    ((x, z), w) := "101";
    y(0 to 1) <= (x, z);
  end process;
end architecture rtl;
