-- DESCRIPTION: Verilator: VHDL clock branches that cannot be modeled
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity last_branch is
  port (
    clk : in std_logic;
    en : in std_logic;
    d : in std_logic;
    q : out std_logic
  );
end;

architecture rtl of last_branch is
begin
  process (clk, en) is
  begin
    -- The later branch also runs when en changes, not only on a clock edge
    if rising_edge(clk) then
      q <= d;
    elsif en = '1' then
      q <= '0';
    end if;
  end process;
end;

library ieee;
use ieee.std_logic_1164.all;

entity async_reset is
  port (
    clk : in std_logic;
    rst1 : in std_logic;
    rst2 : in std_logic;
    d : in std_logic;
    q : out std_logic
  );
end;

architecture rtl of async_reset is
begin
  process (clk, rst1) is
  begin
    -- A complex level condition on a sensitivity-list signal
    if rst1 = '1' or rst2 = '1' then
      q <= '0';
    elsif rising_edge(clk) then
      q <= d;
    end if;
  end process;
end;

library ieee;
use ieee.std_logic_1164.all;

entity shared_target is
  port (
    clk1 : in std_logic;
    clk2 : in std_logic;
    d : in std_logic;
    q : out std_logic
  );
end;

architecture rtl of shared_target is
begin
  process (clk1, clk2) is
  begin
    if rising_edge(clk1) then
      q <= d;
    end if;
    -- Two clocks writing one signal have no defined order
    if rising_edge(clk2) then
      q <= not d;
    end if;
  end process;
end;
