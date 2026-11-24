-- DESCRIPTION: Verilator: a generic map in the binding of a configuration
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity xor_gate is
  generic (invert : boolean);
  port (a : in std_logic;
        b : in std_logic;
        q : out std_logic);
end entity xor_gate;

architecture rtl of xor_gate is
begin
  gen : if invert generate
    q <= not (a xor b);
  else generate
    q <= a xor b;
  end generate;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

-- Configured by a configuration of its own, which the one of t only names
entity inner is
  port (a : in std_logic;
        b : in std_logic;
        q : out std_logic);
end entity inner;

architecture rtl of inner is
  component gate is
    port (a : in std_logic;
          b : in std_logic;
          q : out std_logic);
  end component gate;
begin
  g : gate port map (a => a, b => b, q => q);
end architecture rtl;

configuration inner_conf of inner is
  for rtl
    for g : gate
      use entity work.xor_gate generic map (invert => true);
    end for;
  end for;
end configuration inner_conf;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (x : in std_logic;
        y : in std_logic;
        plain : out std_logic;
        inverted : out std_logic;
        overridden : out std_logic;
        nested : out std_logic);
end entity t;

architecture rtl of t is
  -- The component has no generics and a name of its own: its binding gives them
  component gate is
    port (a : in std_logic;
          b : in std_logic;
          q : out std_logic);
  end component gate;

  -- Its generic map says false; the binding of the configuration says true
  component inner is
    port (a : in std_logic;
          b : in std_logic;
          q : out std_logic);
  end component inner;

  component xor_gate is
    generic (invert : boolean := false);
    port (a : in std_logic;
          b : in std_logic;
          q : out std_logic);
  end component xor_gate;
begin
  g_inv : gate port map (a => x, b => y, q => inverted);
  g_over : xor_gate generic map (invert => false) port map (a => x, b => y, q => overridden);
  g_nested : inner port map (a => x, b => y, q => nested);
  g_plain : xor_gate generic map (invert => false) port map (a => x, b => y, q => plain);
end architecture rtl;

configuration t_conf of t is
  for rtl
    for g_inv : gate
      use entity work.xor_gate generic map (invert => true);
    end for;
    for g_over : xor_gate
      use entity work.xor_gate generic map (invert => true);
    end for;
    for g_nested : inner
      use configuration work.inner_conf;
    end for;
  end for;
end configuration t_conf;
