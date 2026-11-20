-- DESCRIPTION: Verilator: VHDL std_ulogic generic actuals and defaults
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity lvl_unit is
  generic (
    level : std_ulogic := '1'
  );
  port (
    q : out std_ulogic
  );
end entity lvl_unit;

architecture rtl of lvl_unit is
begin
  q <= level;
end architecture rtl;


library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    q0 : out std_ulogic;
    q1 : out std_ulogic;
    q2 : out std_ulogic
  );
end entity t;

architecture rtl of t is
begin
  u0 : entity work.lvl_unit
    generic map (level => '1')
    port map (q => q0);

  -- Default value
  u1 : entity work.lvl_unit
    port map (q => q1);

  u2 : entity work.lvl_unit
    generic map (level => '0')
    port map (q => q2);
end architecture rtl;
