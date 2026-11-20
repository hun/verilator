-- DESCRIPTION: Verilator: a port associated twice in one instance
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (a : in std_logic; y : out std_logic);
end entity;

architecture rtl of leaf is
begin
  y <= a;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t_dup is
  port (x : in std_logic; z : in std_logic; o : out std_logic);
end entity;

architecture rtl of t_dup is
begin
  u : entity work.leaf port map (a => x, a => z, y => o);
end architecture;
