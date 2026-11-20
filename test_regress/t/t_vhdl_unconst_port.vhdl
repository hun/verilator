-- DESCRIPTION: Verilator: VHDL unconstrained entity ports
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (
    i : in std_logic_vector;
    o : out std_logic_vector
  );
end;

architecture rtl of child is
begin
  o <= not i;
end;

library ieee;
use ieee.std_logic_1164.all;

entity defchild is
  port (
    d : in std_logic_vector := x"a5";
    o : out std_logic_vector
  );
end;

architecture rtl of defchild is
begin
  o <= d;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(7 downto 0);
    y : out std_logic_vector(3 downto 0);
    v : out std_logic_vector(3 downto 0);
    z : out std_logic_vector(7 downto 0);
    w : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
  component child is
    port (
      i : in std_logic_vector;
      o : out std_logic_vector
    );
  end component;
begin
  -- Two specializations of the same unconstrained entity
  u1 : entity work.child port map (i => a, o => y);
  u2 : child port map (i => b, o => z);
  -- A slice actual
  u3 : entity work.child port map (i => b(7 downto 4), o => v);
  -- An omitted input port takes its declared default
  ud : entity work.defchild port map (o => w);
end;
