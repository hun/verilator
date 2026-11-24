-- DESCRIPTION: Verilator: VHDL a metavalue test in a boolean expression
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_is_x_boolean is
  port (ra  : in  std_logic_vector(3 downto 0);
        ren : in  std_logic;
        y   : out std_logic);
end t_vhdl_is_x_boolean;

architecture behav of t_vhdl_is_x_boolean is
begin
  process (ra, ren)
  begin
    -- is_x is a boolean, so it combines with a comparison
    if (ren = '1') and not is_x (ra) then
      y <= '1';
    else
      y <= '0';
    end if;
  end process;
end behav;
