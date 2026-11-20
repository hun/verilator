-- DESCRIPTION: Verilator: VHDL standard intrinsic operand diagnostics
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Every call below has no matching standard-package overload (IEEE 1164
-- 3.2.6, IEEE 1076-2008 16.8.5). GHDL rejects each with "no overloaded
-- function found"; the frontend must not lower them.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    n : in  integer;
    v : in  std_logic_vector(3 downto 0);
    u : out unsigned(3 downto 0);
    y : out bit
  );
end entity;

architecture rtl of t is
begin
  process (n, v)
  begin
    if rising_edge(n) then
      y <= '1';
    end if;
    if rising_edge(v) then
      y <= '0';
    end if;
  end process;

  u <= to_unsigned(to_integer(n), 4);
  y <= '1' when to_integer(v) = 0 else '0';
end architecture;
