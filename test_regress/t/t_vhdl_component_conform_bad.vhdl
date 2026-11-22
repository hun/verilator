-- DESCRIPTION: Verilator: VHDL component interfaces must match the bound entity
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- IEEE 1076-2008 6.5.2.2: a component declaration's ports must conform to the
-- entity the instantiation binds.

library ieee;
use ieee.std_logic_1164.all;

entity leaf_rename is
  port (
    a : in  std_logic;
    b : out integer
  );
end;

architecture rtl of leaf_rename is
begin
  b <= 1;
end;

library ieee;
use ieee.std_logic_1164.all;

entity leaf_type is
  port (
    a : in  std_logic;
    b : out integer
  );
end;

architecture rtl of leaf_type is
begin
  b <= 1;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i : in  std_logic;
    o : out integer
  );
end;

architecture rtl of t is
  component leaf_rename is
    port (
      x : in  std_logic;
      y : out integer
    );
  end component;
  component leaf_type is
    port (
      a : in  integer;
      b : out integer
    );
  end component;
begin
  u1 : leaf_rename port map (x => i, y => o);
  u2 : leaf_type port map (a => i, b => o);
end;
