-- DESCRIPTION: Verilator: a record aggregate as a whole-port actual
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package bus_pkg is
  type req_t is record
    addr : std_logic_vector(3 downto 0);
    sel  : std_logic;
  end record;
end package bus_pkg;

library ieee;
use ieee.std_logic_1164.all;
use work.bus_pkg.all;

entity leaf is
  port (req_i : in req_t; y_o : out std_logic_vector(3 downto 0));
end entity leaf;

architecture rtl of leaf is
begin
  y_o <= req_i.addr when req_i.sel = '1' else (others => '0');
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
use work.bus_pkg.all;

entity t is
  port (o : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  u : entity work.leaf
    port map (
      req_i => (addr => "1010", sel => '1'),
      y_o   => o
    );
end architecture rtl;
