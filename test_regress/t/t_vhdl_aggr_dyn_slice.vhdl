-- DESCRIPTION: Verilator: an aggregate element that is a conversion of a slice at a run-time position
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    sel : in  std_logic_vector(1 downto 0);
    d   : in  std_logic_vector(10 downto 0);
    o   : out std_logic_vector(4 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  process (sel, d)
    variable idx : integer;
    variable v   : signed(4 downto 0);
  begin
    idx := to_integer(unsigned(sel));
    -- The width of the second element is only known from the slice's length
    v := ("0", signed(d(idx + 3 downto idx)));
    o <= std_logic_vector(v);
  end process;
end architecture rtl;
