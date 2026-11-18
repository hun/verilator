-- DESCRIPTION: Verilator: string generics selecting generate branches
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A string generic's text is fixed at elaboration: comparing it with a literal in
-- a generate condition selects the branch, for explicit bindings, for the default,
-- and for an empty string (which has no packed value to compare).

library ieee;
use ieee.std_logic_1164.all;

entity mode_gate is
  generic (MODE : string := "PASS"; INIT : string := "x");
  port (i : in std_logic_vector(3 downto 0); o : out std_logic_vector(3 downto 0);
        empty : out std_logic);
end entity;

architecture rtl of mode_gate is
begin
  g_pass : if MODE = "PASS" generate
    o <= i;
  end generate;

  g_inv : if MODE /= "PASS" generate
    o <= not i;
  end generate;

  g_empty : if INIT = "" generate
    empty <= '1';
  end generate;

  g_set : if INIT /= "" generate
    empty <= '0';
  end generate;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i     : in  std_logic_vector(3 downto 0);
    o_pass : out std_logic_vector(3 downto 0);
    o_inv  : out std_logic_vector(3 downto 0);
    o_def  : out std_logic_vector(3 downto 0);
    e_pass : out std_logic;
    e_inv  : out std_logic
  );
end entity;

architecture rtl of t is
begin
  u_pass : entity work.mode_gate
    generic map (MODE => "PASS", INIT => "")
    port map (i => i, o => o_pass, empty => e_pass);

  u_inv : entity work.mode_gate
    generic map (MODE => "INV", INIT => "a,b")
    port map (i => i, o => o_inv, empty => e_inv);

  u_def : entity work.mode_gate
    generic map (INIT => "")
    port map (i => i, o => o_def, empty => open);
end architecture;
