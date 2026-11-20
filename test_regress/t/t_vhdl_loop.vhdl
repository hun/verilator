-- DESCRIPTION: Verilator: VHDL for loop, dynamic index and signed slice
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
    a   : in std_logic_vector(3 downto 0);
    idx : in integer range 0 to 3;
    s   : in signed(3 downto 0);
    y   : out std_logic;
    z   : out std_logic_vector(3 downto 0);
    u   : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
begin
  -- Dynamic index
  y <= a(idx);
  -- A slice of a signed array stays signed and sign-extends on resize
  u <= std_logic_vector(resize(s(3 downto 0), 8));

  -- Static loop range, unrolled
  process (a) is
  begin
    for i in 0 to 3 loop
      z(i) <= a(i);
    end loop;
  end process;
end;
