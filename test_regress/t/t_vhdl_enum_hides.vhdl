-- DESCRIPTION: Verilator: a declaration hides an enumeration literal of a used package
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- `p` declares `ra` in two enumeration types. The port named `ra` shadows both,
-- so the literal is not visible in the architecture; the a_t value is named by
-- its low bound instead.

package enum_hides_pkg is
  type a_t is (ra, rbb);
  type b_t is (rcc, ra);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.enum_hides_pkg.all;

entity t is
  port (
    ra    : in  std_logic;  -- the object, which hides both imported literals
    o_sel : out std_logic;
    o_lit : out std_logic
  );
end entity;

architecture rtl of t is
  -- The port object `ra` hides both imported literals, so name the a_t value by
  -- its declared low bound instead of by the hidden literal.
  signal lit : a_t;
begin
  lit <= a_t'low;

  o_sel <= ra;  -- the object
  o_lit <= '1' when lit = rbb else '0';  -- a_t's `ra` is 0, so lit is never rbb
end architecture;
