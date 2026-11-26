-- DESCRIPTION: Verilator: a deferred constant sizing a record element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package config is
  constant width : integer;
end package config;

library ieee;
use ieee.std_logic_1164.all;
use work.config.all;

-- The record is declared before the body that gives `width` its value
package types is
  type rec is record
    x : std_logic_vector(1 to width);
  end record;
end package types;

package body config is
  constant width : integer := 6;
end package body config;

library ieee;
use ieee.std_logic_1164.all;
use work.types.all;

entity t is
  port (d : in std_logic_vector(5 downto 0);
        q : out std_logic_vector(5 downto 0));
end entity t;

architecture rtl of t is
  signal r : rec;
begin
  r.x <= d;
  q <= r.x;
end architecture rtl;
