-- DESCRIPTION: Verilator: VHDL conditional and selected assignments
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    sel : in std_logic;
    a   : in std_logic;
    b   : in std_logic;
    y   : out std_logic;
    z   : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
begin
  y <= a when sel = '1' else b;
  with sel select z <= "00" when '0', "11" when others;
end;
