-- DESCRIPTION: Verilator: an entity no instance names need not have its generics bound
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

-- Nothing instantiates this entity, so its generic never gets a value
entity unused_e is
  generic (width : natural);
  port (a : in std_logic_vector(width - 1 downto 0); y : out std_logic);
end entity unused_e;

architecture rtl of unused_e is
begin
  y <= a(0);
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  o <= "1010";
end architecture rtl;
